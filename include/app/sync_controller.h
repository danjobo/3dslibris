/*
    3dslibris - sync_controller.h

    "Sync with another 3DS" screen (AppMode::Sync). One console hosts and
    shows a pairing code; the other joins and types the code. Over Wi-Fi the
    two exchange their libraries' per-book state (progress, bookmarks,
    highlights and notes), merge it (see sync/sync_session.h) and apply the
    result. Everything runs on the main loop without blocking.
*/

#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
  enum Screen { kMenu, kHosting, kJoining, kSyncing, kSummary, kError };

  bool StartNetwork(uint32_t *local_ip);
  void StopNetwork();
  void StartHost();
  void StartJoin();
  void EndSession();
  void ShowError(const std::string &message);
  void ApplyResults();
  sync_manifest::Manifest BuildLocalManifest();
  std::string DeviceName() const;
  void Draw();
  void Leave();

  App &app_;
  Screen screen_;
  int menu_index_;
  bool dirty_;
  std::unique_ptr<SyncTransport> transport_;
  std::unique_ptr<SyncSession> session_;
  std::vector<std::pair<std::string, Book *> > books_by_sync_id_;
  std::string pairing_code_;
  std::string message_;
  std::vector<std::string> summary_lines_;
  int last_phase_;
  uint32_t *soc_buffer_;
  bool soc_ready_;
};
