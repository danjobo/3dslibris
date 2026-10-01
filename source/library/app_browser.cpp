/*
    3dslibris - app_browser.cpp
    Adapted from dslibris for Nintendo 3DS.

    Original attribution (dslibris): Ray Haleblian, GPLv2+.
    Modified for Nintendo 3DS by Rigle.

    Changes by Rigle (summary):
    - Native FS-based book discovery and UTF-8 normalization hardening.
    - Browser cover cache/preload integration and deferred metadata jobs.
    - Touch navigation and footer interactions adapted to 3DS orientation.
*/

#include "app/app.h"
#include "app/library_controller.h"
#include "shared/orientation_utils.h"
#include "shared/screen_dimensions.h"

#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>

#include <3ds.h>

#include "book/book.h"
#include "book/book_parser.h"
#include "ui/browser_nav.h"
#include "formats/common/book_error.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "menus/chapter_menu.h"
#include "shared/debug_log.h"
#include "formats/cbz/cbz_parser.h"
#include "formats/epub/epub_parser.h"
#include "formats/fb2/fb2_parser.h"
#include "formats/mobi/mobi_parser.h"
#include "formats/pdf/pdf_parser.h"
#include "parse.h"
#include "shared/app_flow_utils.h"
#include "shared/color_utils.h"
#include "library/browser_cover_cache_utils.h"
#include "library/browser_folder_input_utils.h"
#include "library/cover_cache.h"
#include "library/browser_grid_view.h"
#include "library/browser_job_queue_utils.h"
#include "library/browser_list_view.h"
#include "library/browser_shelf_view.h"
#include "library/browser_presentation_utils.h"
#include "library/browser_view_utils.h"
#include "library/browser_warmup_utils.h"
#include "library/library_details_view.h"
#include "library/library_draw.h"
#include "shared/debug_runtime_mode.h"
#include "shared/string_utils.h"
#include "settings/prefs.h"
#include "shared/path_constants.h"
#include "ui/text.h"
#include "shared/utf8_utils.h"
#include "app/version.h"

#ifndef UTF8_FILENAME_DIAG
#define UTF8_FILENAME_DIAG 0
#endif

#ifndef BROWSER_COVER_TRACE
#define BROWSER_COVER_TRACE 0
#endif

#ifndef BROWSER_JOB_TRACE
#define BROWSER_JOB_TRACE 0
#endif

namespace {

static BrowserViewMode CurrentBrowserViewMode(const App &app) {
  if (!app.prefs)
    return BROWSER_VIEW_GALLERY;
  return app.prefs->browser_view_mode;
}

static int CurrentBrowserPageSize(const App &app) {
  return browser_view_utils::PageSize(CurrentBrowserViewMode(app));
}

static int CurrentBrowserColumnCount(const App &app) {
  return browser_view_utils::ColumnCount(CurrentBrowserViewMode(app));
}

static void LayoutBrowserNavButtons(App *app) {
  const bool in_folder = app->IsBrowserInsideFolder();
  app->buttonback.Move(in_folder ? 130 : screen_layout::kFooterLeftX, screen_layout::kFooterY);
  app->buttonback.Resize(52, screen_layout::kFooterButtonH);
  app->buttonback.Label("back");
  app->buttonback.SetIcon(UI_BUTTON_ICON_BACK);

  app->buttonprev.Move(screen_layout::kFooterLeftX, screen_layout::kFooterY);
  app->buttonprev.Resize(in_folder ? 52 : screen_layout::kFooterNavW, screen_layout::kFooterButtonH);
  app->buttonprev.Label("prev");
  app->buttonprev.SetIcon(UI_BUTTON_ICON_PREV);

  app->buttonnext.Move(in_folder ? 186 : screen_layout::kFooterRightX, screen_layout::kFooterY);
  app->buttonnext.Resize(in_folder ? 52 : screen_layout::kFooterNavW, screen_layout::kFooterButtonH);
  app->buttonnext.Label("next");
  app->buttonnext.SetIcon(UI_BUTTON_ICON_NEXT);

  app->buttonprefs.Move(in_folder ? 56 : screen_layout::kFooterMidX, screen_layout::kFooterY);
  app->buttonprefs.Resize(in_folder ? 70 : screen_layout::kFooterMidW, screen_layout::kFooterButtonH);
  app->buttonprefs.Label("settings");
  app->buttonprefs.SetIcon(UI_BUTTON_ICON_GEAR);
}

} // namespace

// NOTE: UnloadNonVisibleBrowserCoverCaches / LoadVisibleBrowserCoverCaches
// moved to app_browser_covers.cpp.

namespace {

static BrowserGridMarqueeState g_marquee;

} // namespace

void LibraryController::ResetBrowserMarquee() { g_marquee.Reset(); }

// NOTE: job-queue and warmup methods (HasQueuedJob, PrioritizeSelectedBookJobs,
// EnqueueJob, QueueBookWarmup, TickBrowserWarmup, QueueTocResolve, ProcessJobs,
// PauseBrowserJobs) moved to app_browser_jobs.cpp.

// Holding X this long on a book opens the delete screen (as in the reader,
// where holding X starts text selection).
static const uint64_t kDeleteHoldMs = 400;

static void CycleColorMode(App &app) {
  const int next = (app.ts->GetColorMode() + 1) % 6;
  app.colorMode = next;
  app.ts->SetColorMode(next);
  UiButtonSkin_SetColorMode(next);
  app.ts->MarkAllScreensDirty();
  g_marquee.Reset();
  app.SetBrowserDirty(true);
}

void LibraryController::RefreshCurrentFolder(int select_index) {
  if (inside_folder_) {
    const std::string path = current_folder_path_;
    const std::string name = current_folder_name_;
    LoadFolderPath(path, name);
  } else {
    RebuildRoot();
  }
  const int count = app_.BookCount();
  if (count <= 0)
    return;
  const int index = std::max(0, std::min(select_index, count - 1));
  app_.SetSelectedBook(app_.books[index]);
  const int page_size = CurrentBrowserPageSize(app_);
  if (page_size > 0)
    app_.SetBrowserPageStart(index - index % page_size);
  LoadVisibleBrowserCoverCaches();
  app_.SetBrowserDirty(true);
}

void LibraryController::browser_handleevent(const FrameInput &input) {
  if (app_.ShouldAbortWork())
    return;

  // Re-apply browser layout in case another view reused/moved shared buttons.
  LayoutBrowserNavButtons(&app_);

  const u32 keys = input.keys_down;
#ifdef DSLIBRIS_DEBUG
  if (keys) {
    DBG_LOGF(&app_, "BROWSER handleevent keys=0x%08lx",
             (unsigned long)keys);
  }
#endif
  auto map_grid_nav = [&](u32 key_down, BrowserNavMove *move) -> bool {
    const u32 nav_down = app_.key.ddown | app_.key.down;
    const u32 nav_up = app_.key.dup | app_.key.up;
    const u32 nav_left = app_.key.dleft | app_.key.left;
    const u32 nav_right = app_.key.dright | app_.key.right;
    if (!move)
      return false;
    if (!orientation_utils::IsTurnedRight(app_.render_orientation)) {
      // Turned Left (right-handed): rotate d-pad mapping so directional input
      // follows the visual page orientation.
      if (key_down & nav_down) {
        *move = BROWSER_NAV_RIGHT;
        return true;
      }
      if (key_down & nav_up) {
        *move = BROWSER_NAV_LEFT;
        return true;
      }
      if (key_down & nav_left) {
        *move = BROWSER_NAV_DOWN;
        return true;
      }
      if (key_down & nav_right) {
        *move = BROWSER_NAV_UP;
        return true;
      }
      return false;
    }

    // Turned Right (left-handed): rotate d-pad mapping so directional intent
    // matches the on-screen grid orientation.
    if (key_down & nav_down) {
      *move = BROWSER_NAV_LEFT;
      return true;
    }
    if (key_down & nav_up) {
      *move = BROWSER_NAV_RIGHT;
      return true;
    }
    if (key_down & nav_left) {
      *move = BROWSER_NAV_UP;
      return true;
    }
    if (key_down & nav_right) {
      *move = BROWSER_NAV_DOWN;
      return true;
    }
    return false;
  };
  const u32 release_mask = KEY_TOUCH | app_.key.a | app_.key.b | app_.key.x |
                           app_.key.y | app_.key.start | app_.key.select |
                           app_.key.dup | app_.key.ddown | app_.key.dleft |
                           app_.key.dright | app_.key.up | app_.key.down |
                           app_.key.left | app_.key.right | app_.key.l |
                           app_.key.r | app_.key.zl | app_.key.zr;
  if (app_.IsBrowserWaitingInputRelease()) {
    const u64 now = input.timestamp_ms;
    if (input.keys_held & release_mask &&
        !browser_warmup_utils::ShouldForceClearInputRelease(
            now, app_.GetBrowserLastInteractionMs(), true))
      return;
    app_.SetBrowserWaitingInputRelease(false);
    if (browser_warmup_utils::ShouldForceClearInputRelease(
            now, app_.GetBrowserLastInteractionMs(), true))
      app_.SetBrowserLastInteractionMs(now);
    return;
  }

  // X was pressed earlier: released soon it cycles the theme, held on a
  // book it offers to delete the book.
  if (x_hold_armed_) {
    if (!(input.keys_held & app_.key.x)) {
      x_hold_armed_ = false;
      CycleColorMode(app_);
    } else if (input.timestamp_ms - x_down_ms_ >= kDeleteHoldMs) {
      x_hold_armed_ = false;
      Book *selected = app_.GetSelectedBook();
      if (selected && !selected->IsBrowserFolder()) {
        app_.ShowDeleteBookView(selected);
        return;
      }
    }
  }

  auto navigateSelection = [&](BrowserNavMove move) {
    if (app_.BookCount() <= 0)
      return;
    const int old_page_start = app_.GetBrowserPageStart();
    Book *old_selected = app_.GetSelectedBook();
    const int page_size = CurrentBrowserPageSize(app_);
    const int columns = CurrentBrowserColumnCount(app_);
    const int old_index = app_.GetBookIndex(app_.GetSelectedBook());
    if (old_index < 0)
      return;
    const int old_index_on_page = old_index - old_page_start;
    const int books_remaining = app_.BookCount() - old_page_start;
    const int visible_on_page =
        (books_remaining < page_size) ? books_remaining : page_size;

    // At the edges of the current page, overflow d-pad navigation into a page
    // flip rather than clamping in place.
    if (columns == 1) {
      // List mode: top item + backward → prev page; bottom item + forward → next page.
      if (move == BROWSER_NAV_LEFT && old_index_on_page == 0) {
        browser_prevpage();
        return;
      }
      if (move == BROWSER_NAV_RIGHT && old_index_on_page == visible_on_page - 1) {
        browser_nextpage();
        return;
      }
    } else {
      // Gallery mode: left-column item + left → prev page; right-column item + right → next page.
      if (move == BROWSER_NAV_LEFT && old_index_on_page % columns == 0) {
        browser_prevpage();
        return;
      }
      if (move == BROWSER_NAV_RIGHT &&
          old_index_on_page % columns == columns - 1) {
        browser_nextpage();
        return;
      }
    }

    BrowserNavState state = {old_index, old_page_start};
    state = BrowserNavMoveSelection(state, app_.BookCount(), page_size, columns,
                                    move);
    if (state.selected_index < 0 || state.selected_index >= app_.BookCount())
      return;
    app_.SetBrowserPageStart(state.page_start);
    app_.SetSelectedBook(app_.books[state.selected_index]);
    if (app_.GetBrowserPageStart() != old_page_start ||
        CurrentBrowserViewMode(app_) == BROWSER_VIEW_LIST)
      LoadVisibleBrowserCoverCaches();
    if (app_.GetSelectedBook() != old_selected) {
      g_marquee.Reset();
      PrioritizeSelectedBookJobs(app_.GetSelectedBook());
      app_.SetBrowserLastInteractionMs(osGetTime());
    }
    app_.SetBrowserDirty(true);
  };

  BrowserNavMove nav_move = BROWSER_NAV_LEFT;
  const bool has_grid_nav = map_grid_nav(keys, &nav_move);

  if (browser_folder_input_utils::ShouldLeaveFolder(keys, app_.key.b,
                                                    app_.key.start)) {
    if (IsInsideFolder())
      LeaveFolder();
  } else if (keys & app_.key.a) {
    OpenSelectedBrowserEntry();
  } else if (has_grid_nav) {
    navigateSelection(nav_move);
  } else if (keys & app_.key.l) {
    browser_prevpage();
  } else if (keys & app_.key.r) {
    browser_nextpage();
  }

  else if (keys & app_.key.x) {
    // Decided on release or after the hold time (see above).
    x_hold_armed_ = true;
    x_down_ms_ = input.timestamp_ms;
  }

  else if (keys & (app_.key.select | app_.key.y)) {
    app_.ShowSettingsView(false);
  }

  else if (keys & KEY_TOUCH) {
    auto hitsButtonAt = [&](Button &button, int px, int py, int slack) {
      if (slack <= 0)
        return button.EnclosesPoint((u16)px, (u16)py);
      for (int dy = -slack; dy <= slack; dy += slack) {
        for (int dx = -slack; dx <= slack; dx += slack) {
          int x = px + dx;
          int y = py + dy;
          if (x < 0 || y < 0 || x > 239 || y > 319)
            continue;
          if (button.EnclosesPoint((u16)x, (u16)y))
            return true;
        }
      }
      return false;
    };

    auto handleTouchAt = [&](int x, int y) -> bool {
      if (x < 0 || y < 0 || x > 239 || y > 319)
        return false;

      if (hitsButtonAt(app_.buttonnext, x, y, 4)) {
        browser_nextpage();
        return true;
      }
      if (hitsButtonAt(app_.buttonprev, x, y, 4)) {
        browser_prevpage();
        return true;
      }
      if (hitsButtonAt(app_.buttonprefs, x, y, 4)) {
        app_.ShowSettingsView(false);
        return true;
      }
      if (IsInsideFolder() && hitsButtonAt(app_.buttonback, x, y, 4)) {
        LeaveFolder();
        return true;
      }

      // Prefer coarse cell hit-test (cover + title/progress area):
      // single tap selects, tapping selected book opens.
      int book_idx = -1;
      if (CurrentBrowserViewMode(app_) == BROWSER_VIEW_LIST) {
        book_idx = browser_list_view::HitTestBookIndex(
            x, y, app_.GetBrowserPageStart(), app_.BookCount(),
            CurrentBrowserPageSize(app_));
      } else if (CurrentBrowserViewMode(app_) == BROWSER_VIEW_SHELF) {
        book_idx = browser_shelf_view::HitTestBookIndex(
            x, y, app_.GetBrowserPageStart(), app_.BookCount());
      } else {
        book_idx = browser_grid_view::HitTestBookIndex(
            x, y, app_.GetBrowserPageStart(), app_.BookCount());
        if (book_idx < 0) {
          for (int i = app_.GetBrowserPageStart();
               (i < app_.BookCount()) &&
               (i < app_.GetBrowserPageStart() + APP_BROWSER_BUTTON_COUNT);
               i++) {
            if (hitsButtonAt(*app_.buttons[i], x, y, 4)) {
              book_idx = i;
              break;
            }
          }
        }
      }
      if (book_idx >= 0 && book_idx < app_.BookCount()) {
        if (app_.GetSelectedBook() == app_.books[book_idx]) {
          OpenSelectedBrowserEntry();
        } else {
          app_.SetSelectedBook(app_.books[book_idx]);
          if (CurrentBrowserViewMode(app_) == BROWSER_VIEW_LIST)
            LoadVisibleBrowserCoverCaches();
          g_marquee.Reset();
          PrioritizeSelectedBookJobs(app_.GetSelectedBook());
          app_.SetBrowserLastInteractionMs(osGetTime());
          app_.SetBrowserDirty(true);
        }
        return true;
      }
      return false;
    };

    touchPosition mapped = app_.MapTouch(input);
    handleTouchAt((int)mapped.px, (int)mapped.py);
  }
}

void LibraryController::browser_init(void) {
  large_cover_checked_ = nullptr;
  for (int i = 0; i < app_.BookCount(); i++) {
    int page_idx = i % APP_BROWSER_BUTTON_COUNT;
    int col = page_idx % browser_grid_view::kGridCols;
    int row = page_idx / browser_grid_view::kGridCols;

    app_.buttons.push_back(new Button());
    app_.buttons[i]->Init(app_.ts.get());
    app_.buttons[i]->Resize(browser_grid_view::kCoverW + 4,
                            browser_grid_view::kCoverH + 4);
    app_.buttons[i]->Move(browser_grid_view::kGridX0 +
                              col * browser_grid_view::kCellW,
                          browser_grid_view::kGridY0 +
                              row * browser_grid_view::kCellH);

    // Cover extraction moved to browser_draw to avoid freezing at startup
    app_.buttons[i]->SetLabel1(std::string(""));

  }

  app_.buttonprev.Init(app_.ts.get());
  app_.buttonnext.Init(app_.ts.get());
  app_.buttonprefs.Init(app_.ts.get());
  app_.buttonback.Init(app_.ts.get());
  LayoutBrowserNavButtons(&app_);

  if (app_.BookCount() <= 0) {
    app_.SetBrowserPageStart(0);
    app_.SetSelectedBook(NULL);
    app_.SetBrowserLastInteractionMs(osGetTime());
    app_.SetBrowserDirty(true);
    return;
  }

  if (!app_.GetSelectedBook()) {
    app_.SetBrowserPageStart(0);
    app_.SetSelectedBook(app_.books[0]);
  } else {
    app_.SetBrowserPageStart(
        (app_.GetBookIndex(app_.GetSelectedBook()) / CurrentBrowserPageSize(app_)) *
        CurrentBrowserPageSize(app_));
  }
  g_marquee.Reset();
  PrioritizeSelectedBookJobs(app_.GetSelectedBook());
  app_.SetBrowserLastInteractionMs(osGetTime());
  LoadVisibleBrowserCoverCaches();
}

void LibraryController::browser_nextpage() {
  const int page_size = CurrentBrowserPageSize(app_);
  if (app_.GetBrowserPageStart() + page_size < app_.BookCount()) {
    app_.SetBrowserPageStart(app_.GetBrowserPageStart() + page_size);
    app_.SetSelectedBook(app_.books[app_.GetBrowserPageStart()]);
    g_marquee.Reset();
    PrioritizeSelectedBookJobs(app_.GetSelectedBook());
    app_.SetBrowserLastInteractionMs(osGetTime());
    LoadVisibleBrowserCoverCaches();
    app_.SetBrowserDirty(true);
  }
}

void LibraryController::browser_prevpage() {
  const int page_size = CurrentBrowserPageSize(app_);
  if (app_.GetBrowserPageStart() >= page_size) {
    app_.SetBrowserPageStart(app_.GetBrowserPageStart() - page_size);
    app_.SetSelectedBook(
        app_.books[app_.GetBrowserPageStart() + page_size - 1]);
    g_marquee.Reset();
    PrioritizeSelectedBookJobs(app_.GetSelectedBook());
    app_.SetBrowserLastInteractionMs(osGetTime());
    LoadVisibleBrowserCoverCaches();
    app_.SetBrowserDirty(true);
  }
}

void LibraryController::browser_draw(void) {
  // Keep footer controls stable after view switches.
  LayoutBrowserNavButtons(&app_);

  // save state
  int colorMode = app_.ts->GetColorMode();
  u16 *screen = app_.ts->GetScreen();
  int style = app_.ts->GetStyle();
  int savedPixelSize = app_.ts->pixelsize;

  app_.ts->SetScreen(app_.ts->screenleft);
  app_.ts->SetStyle(TEXT_STYLE_BROWSER);
  Book *selected = app_.GetSelectedBook();
  PrepareSelectedLargeCover();
  if (selected) {
    // Top screen: the selected book's cover and reading progress.
    app_.DrawTopGradientBackground();
    library_details_view::Draw(app_.ts.get(), selected);
  } else {
    app_.ts->PrintSplash(app_.ts->screenleft);
    char versionMsg[16];
    snprintf(versionMsg, sizeof(versionMsg), "v%s", VERSION);
    const int versionWidth =
        app_.ts->GetStringWidth(versionMsg, TEXT_STYLE_BROWSER);
    int versionX = (screen_dims::kTopScreenWidthPx - versionWidth) / 2;
    if (versionX < 0)
      versionX = 0;
    app_.ts->SetPixelSize(10);
    app_.ts->SetPen(versionX, 57);
    app_.ts->PrintString(versionMsg);
  }

  app_.ts->SetScreen(app_.ts->screenright);
  app_.ts->ClearScreen();
  app_.DrawBottomGradientBackground();

  const BrowserViewMode view_mode = CurrentBrowserViewMode(app_);
  const int page_size = CurrentBrowserPageSize(app_);

  BrowserDrawContext ctx{app_.ts.get(), &app_.books, app_.GetSelectedBook(),
                         &app_.buttons};
  {
    library_draw::TextStateGuard guard(app_.ts.get());
    if (view_mode == BROWSER_VIEW_LIST)
      browser_list_view::DrawPage(ctx, app_.GetBrowserPageStart(), page_size);
    else if (view_mode == BROWSER_VIEW_SHELF)
      browser_shelf_view::DrawPage(ctx, app_.GetBrowserPageStart());
    else
      browser_grid_view::DrawPage(ctx, g_marquee, app_.GetBrowserPageStart());
  }

  app_.ts->SetPixelSize(savedPixelSize);

  if (app_.IsBrowserInsideFolder())
    app_.buttonback.Draw(app_.ts->screenright, false);
  if (app_.GetBrowserPageStart() >= page_size)
    app_.buttonprev.Draw(app_.ts->screenright, false);
  if (app_.BookCount() > app_.GetBrowserPageStart() + page_size)
    app_.buttonnext.Draw(app_.ts->screenright, false);

  app_.buttonprefs.Draw(app_.ts->screenright, false);

  if (app_.BookCount() > page_size) {
    int currentPage = (app_.GetBrowserPageStart() / page_size) + 1;
    int totalPages = (app_.BookCount() + page_size - 1) / page_size;
    char pageMsg[32];
    snprintf(pageMsg, sizeof(pageMsg), "%d/%d", currentPage, totalPages);
    app_.ts->SetPixelSize(8);
    app_.ts->SetPen(112, screen_layout::kFooterY + 3);
    app_.ts->PrintString(pageMsg);
    app_.ts->SetPixelSize(savedPixelSize);
  }

  app_.ts->SetColorMode(colorMode);
  app_.ts->SetScreen(screen);
  app_.ts->SetStyle(style);

  app_.SetBrowserDirty(false);
}

void LibraryController::browser_tick_marquee() {
  browser_grid_view::TickMarquee(app_.ts.get(), g_marquee);
}
