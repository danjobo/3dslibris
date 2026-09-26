/*
    3dslibris - app_lifecycle.cpp
    Extracted from app.cpp. Holds the APT applet lifecycle path
    (PrepareForShutdown, AptHookCallback, HandleAppletHook,
    HandleAppletSuspend, HandleAppletResume).

    See the comments in HandleAppletHook for why logging and SD I/O are
    forbidden inside the hook callback — touching this file requires
    understanding the HOME menu acknowledgment timing window.

    No behavior change — pure code motion.
*/

#include "app/app.h"

#include <3ds.h>

#include "book/book.h"
#include "book/book_renderer.h"
#include "shared/debug_log.h"
#include "ui/text.h"

void App::PrepareForShutdown()
{
  if (lifecycle_state_.IsShutdownPrepared())
    return;
  lifecycle_state_.MarkShutdownPrepared();

#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this,
           "SHUTDOWN begin env=%s mode=%d current_session=%u opening_session=%u suspended=%u",
           lifecycle_state_.IsHomebrew() ? "3dsx/homebrew" : "cia/title", (int)nav_.mode,
           reader_state_.current_book_session_id, reader_state_.opening.session_id,
           lifecycle_state_.IsSuspended() ? 1u : 0u);
#endif

  pending_boot_reopen_ = false;
  skip_next_browser_present_ = false;
  lifecycle_state_.SetResumePending(false);
  lifecycle_state_.SetSuspendHandled(false);
  lifecycle_state_.SetExitRequested(false);
  nav_.browser.wait_input_release = true;
  nav_.browser.last_interaction_ms = osGetTime();
  ResetPageRepeat();

  PersistPrefs();

#ifdef DSLIBRIS_DEBUG
  DBG_LOG(this, "SHUTDOWN cancel workers begin");
#endif
  const size_t removed_jobs = PauseBrowserJobs();
#ifndef DSLIBRIS_DEBUG
  (void)removed_jobs;
#endif

  Book *opening_book = reader_state_.opening.book;
  if (opening_book)
  {
    opening_book->RequestAbortOpen();
    book_renderer::CancelFixedLayoutDeferredWork(opening_book);
    opening_book->CancelAsyncReflowOpen();
  }

  CloseBook();
  nav_.mode = AppMode::Quit;

#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this,
           "SHUTDOWN cancel workers done removed_jobs=%u current_session=%u opening_session=%u",
           (unsigned)removed_jobs, reader_state_.current_book_session_id,
           reader_state_.opening.session_id);
  DBG_LOGF(this, "SHUTDOWN end mode=%d", (int)nav_.mode);
#endif
}

void App::AptHookCallback(APT_HookType hook, void *param)
{
  App *app = static_cast<App *>(param);
  if (app)
    app->HandleAppletHook(hook);
}

namespace
{

app_lifecycle_utils::AptEvent ToAptEvent(APT_HookType hook)
{
  switch (hook)
  {
  case APTHOOK_ONSUSPEND:
    return app_lifecycle_utils::AptEvent::Suspend;
  case APTHOOK_ONRESTORE:
    return app_lifecycle_utils::AptEvent::Restore;
  case APTHOOK_ONSLEEP:
    return app_lifecycle_utils::AptEvent::Sleep;
  case APTHOOK_ONWAKEUP:
    return app_lifecycle_utils::AptEvent::Wakeup;
  case APTHOOK_ONEXIT:
    return app_lifecycle_utils::AptEvent::Exit;
  default:
    return app_lifecycle_utils::AptEvent::Other;
  }
}

} // namespace

void App::HandleAppletHook(APT_HookType hook)
{
  // Do NOT log or touch the SD card here. libctru calls hooks from inside
  // aptMainLoop() while the system waits for this app to acknowledge the
  // transition (for ONSLEEP, APT_ReplySleepNotificationComplete runs right
  // after the hook returns). PersistPrefs()/DBG_LOGF here have previously
  // delayed that acknowledgment past the HOME Menu's timing window and crashed
  // it. Lifecycle events are logged in HandleAppletSuspend/HandleAppletResume
  // instead, and prefs are saved in PrepareForShutdown() on exit.
  const app_lifecycle_utils::AptEvent event = ToAptEvent(hook);
  lifecycle_state_.Apply(
      app_lifecycle_utils::ApplyAptEvent(lifecycle_state_.Snapshot(), event));

  // Sleep is handled entirely inside aptMainLoop() (ONSLEEP, acknowledge,
  // block until wake, ONWAKEUP), so no main-loop frame runs between sleep and
  // wake. Stop the core-1 workers now; leaving them alive across sleep can
  // hang the console on wake, the same failure class as across HOME.
  if (app_lifecycle_utils::ShouldQuiesceWorkersInHook(event))
    QuiesceWorkersForSleep();
}

void App::QuiesceWorkersForSleep()
{
  // Signal-only: no joins, logging, or SD I/O (see HandleAppletHook). The
  // sleep catch-up in HandleAppletResume() finishes the teardown on wake.
  Book *current = reader_state_.bookcurrent;
  Book *opening = reader_state_.opening.book;
  if (current)
    current->SignalBackgroundWorkersShutdown();
  if (opening && opening != current)
  {
    opening->RequestAbortOpen();
    opening->SignalBackgroundWorkersShutdown();
  }
}

void App::HandleAppletSuspend()
{
  if (lifecycle_state_.IsSuspendHandled())
    return;
#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this,
           "APPLET suspend begin mode=%d current_session=%u opening_session=%u",
           (int)nav_.mode, reader_state_.current_book_session_id,
           reader_state_.opening.session_id);
#endif
  lifecycle_state_.SetSuspendHandled(true);
  nav_.browser.wait_input_release = true;
  nav_.browser.last_interaction_ms = osGetTime();
  ResetPageRepeat();
  const size_t removed_jobs = PauseBrowserJobs();
#ifndef DSLIBRIS_DEBUG
  (void)removed_jobs;
#endif
#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this, "APPLET suspend workers paused removed_jobs=%u",
           (unsigned)removed_jobs);
#endif
  OnReaderAppletSuspended();
  // Free FreeType glyph bitmap cache to release RAM for the HOME menu.
  // Bounded at 512 glyphs per face × multiple faces × per-glyph buffer alloc
  // — can be hundreds of KB. Cache re-warms transparently on resume.
  if (ts)
    ts->ClearCache();
#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this,
           "APPLET suspend end mode=%d current_session=%u opening_session=%u",
           (int)nav_.mode,
           reader_state_.current_book_session_id,
           reader_state_.opening.session_id);
#endif
}

void App::HandleAppletResume()
{
  if (!lifecycle_state_.IsResumePending())
    return;
  const bool from_sleep = lifecycle_state_.IsSleepCatchupPending();
  if (from_sleep)
  {
    // The workers were only signaled inside ONSLEEP. Run the regular suspend
    // cleanup now (progress save, fixed-layout worker joins, cancelling an
    // in-flight open) so the resume below starts from a consistent state.
    lifecycle_state_.SetSleepCatchupPending(false);
#ifdef DSLIBRIS_DEBUG
    DBG_LOG(this, "APPLET woke from sleep: running suspend catch-up");
#endif
    HandleAppletSuspend();
  }
  lifecycle_state_.SetResumePending(false);
  lifecycle_state_.SetSuspendHandled(false);
  nav_.browser.wait_input_release = true;
  nav_.browser.last_interaction_ms = osGetTime();
  nav_.browser.view_dirty = true;
  ResetPageRepeat();
  nav_.prefs.view_dirty = true;
  if (ts)
    ts->MarkAllScreensDirty();
  RequestStatusRedraw();
  OnReaderAppletResumed();
#ifdef DSLIBRIS_DEBUG
  DBG_LOGF(this, "APPLET resumed mode=%d current_session=%u opening_session=%u",
           (int)nav_.mode, reader_state_.current_book_session_id,
           reader_state_.opening.session_id);
#endif
}
