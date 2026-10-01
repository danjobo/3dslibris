#pragma once

#include <3ds/types.h>
#include <cstddef>
#include <deque>
#include <string>

#include "library/library_job.h"

class App;
struct FrameInput;
class Book;
class Text;

struct LibraryGradientContext {
  Text *ts;
  const u8 *color_mode;
};

class LibraryController {
public:
  explicit LibraryController(App &app);

  int FindBooks();
  void PrepareLibrary();
  void SortBooks();
  void browser_draw();
  void browser_handleevent(const FrameInput &input);
  void browser_init();
  void UnloadNonVisibleBrowserCoverCaches();
  void browser_nextpage();
  void browser_prevpage();
  void LoadVisibleBrowserCoverCaches();
  void PrioritizeSelectedBookJobs(Book *selected_book);
  bool HasQueuedJob(app_job_type_t type, Book *book) const;
  void EnqueueJob(app_job_type_t type, Book *book);
  void TickBrowserWarmup();
  void browser_tick_marquee();
  void ResetBrowserMarquee();
  void QueueBookWarmup(Book *book);
  void QueueTocResolve(Book *book);
  void ProcessJobs(u32 budget_ms);
  size_t PauseBrowserJobs();
  bool IsInsideFolder() const;
  Book *RestoreSavedBookSelection(const char *folder, const char *filename);
  // Rescans the library from the top folder (closes the open book).
  void RebuildRoot();
  // Rescans the folder being shown and selects the entry at select_index
  // (clamped), e.g. the book after one that was deleted.
  void RefreshCurrentFolder(int select_index);

private:
  App &app_;
  std::deque<app_job_t> job_queue_;
  LibraryGradientContext gradient_ctx_;
  bool inside_folder_;
  // X: a short press cycles the theme, holding it on a book offers delete.
  bool x_hold_armed_ = false;
  uint64_t x_down_ms_ = 0;
  std::string current_folder_name_;
  std::string current_folder_path_;

  void EnterFolder(Book *folder);
  void LoadFolderPath(const std::string &folder_path,
                      const std::string &folder_name);
  void LeaveFolder();
  void OpenSelectedBrowserEntry();
};
