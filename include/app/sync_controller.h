/*
    3dslibris - sync_controller.h

    "Sync with another 3DS" screen (AppMode::Sync). One console hosts and
    shows a pairing code; the other joins and types the code. Over Wi-Fi,
    or over local wireless when there is no router, the two exchange their
    whole library's per-book state (progress, bookmarks, highlights and
    notes), merge it (see sync/sync_session.h) and apply the result. Each console can then pick books only the other one has and copy
    them. Everything runs on the main loop without blocking.
*/

#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "sync/sync_book_files.h"
#include "sync/sync_session.h"
#include "sync/sync_transport.h"

class App;
class Book;
struct FrameInput;

class SyncController {
public:
  explicit SyncController(App &app);
  ~SyncController();

  void Show();
  void RunFrame(const FrameInput &input);
  // HOME / sleep interrupt a sync; the connection doesn't survive it.
  void OnAppletSuspended();

private:
  enum Screen {
    kMenu,
    kHosting,
    kJoining,
    kSyncing,      // exchanging state, or waiting for the other console
    kPicking,      // choosing books to copy from the other console
    kTransferring, // copying books here
    kSummary,
    kError,
  };

  // A book file in this console's library, and its Book if it is loaded.
  struct LibraryItem {
    sync_book_files::LocalBook file;
    Book *loaded;
  };

  bool StartNetwork(uint32_t *local_ip);
  // Wi-Fi or local wireless, per local_wireless_; false after ShowError.
  bool OpenTransport(bool host);
  void StopNetwork();
  void StartHost();
  void StartJoin();
  void StartSession();
  void EndSession();
  void ShowError(const std::string &message);
  void ApplyResults();
  void SaveReceivedBooks();
  void BuildSummary();
  void EnterPicker();
  void RunPicker(uint32_t keys, bool touched, int touch_x, int touch_y);
  int PickerVisibleRows() const;
  sync_manifest::Manifest BuildLocalManifest();
  std::string DeviceName() const;
  void Draw();
  void DrawPicker(int y);
  void DrawTransfer(int y);
  void Leave();

  App &app_;
  Screen screen_;
  int menu_index_;
  bool dirty_;
  // Session objects; destroyed in reverse order (session first).
  std::unique_ptr<FileBookSource> source_;
  std::unique_ptr<FileBookSink> sink_;
  std::unique_ptr<SyncTransport> transport_;
  std::unique_ptr<SyncSession> session_;
  std::vector<LibraryItem> library_;
  std::string pairing_code_;
  std::string message_;
  std::vector<std::string> summary_lines_;
  int last_phase_;
  bool results_applied_;
  bool received_saved_;
  uint64_t last_draw_ms_;
  int last_books_sent_;

  // Merge statistics for the summary.
  int changed_books_;
  int records_added_;
  int records_updated_;
  int positions_moved_;
  int links_changed_; // Hardcover links

  // Book picker.
  std::vector<const sync_manifest::BookEntry *> missing_;
  std::vector<bool> picked_;
  int pick_cursor_;
  int pick_top_;

  // Connection type chosen on the menu (kept while the app runs).
  bool local_wireless_;
  uint32_t *soc_buffer_;
  bool soc_ready_;
};
