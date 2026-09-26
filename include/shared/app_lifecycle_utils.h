#pragma once

// Pure APT lifecycle state transitions, kept free of libctru so they can be
// host-tested. app_lifecycle.cpp maps APT_HookType onto AptEvent.
//
// libctru runs every APT hook synchronously on the thread that calls
// aptMainLoop(). Sleep is handled inside aptMainLoop() as
// ONSLEEP -> acknowledge -> block until wake -> ONWAKEUP, and HOME as
// ONSUSPEND -> block in HOME -> ONRESTORE. The main loop therefore only ever
// observes the state after the matching restore/wake hook has already run.

namespace app_lifecycle_utils {

enum class AptEvent {
  Suspend = 0,
  Restore,
  Sleep,
  Wakeup,
  Exit,
  Other,
};

struct LifecycleFlags {
  bool home_suspended;
  bool sleeping;
  bool resume_pending;
  bool suspend_handled;
  bool exit_requested;
  // Set when the console slept. The main-thread suspend cleanup could not run
  // before sleep, so it must run on wake before the normal resume path.
  bool sleep_catchup_pending;

  LifecycleFlags()
      : home_suspended(false), sleeping(false), resume_pending(false),
        suspend_handled(false), exit_requested(false),
        sleep_catchup_pending(false) {}

  bool IsSuspended() const { return home_suspended || sleeping; }
};

LifecycleFlags ApplyAptEvent(LifecycleFlags flags, AptEvent event);

// True when the hook itself must stop background workers before returning.
// Only sleep needs this: libctru acknowledges sleep right after ONSLEEP
// returns, so there is no later main-loop frame to do it in.
bool ShouldQuiesceWorkersInHook(AptEvent event);

} // namespace app_lifecycle_utils
