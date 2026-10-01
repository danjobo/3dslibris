/*
    3dslibris - sync_controller.cpp

    The "sync with another 3DS" screen. See include/app/sync_controller.h.
*/

#include "app/sync_controller.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/app.h"
#include "book/book.h"
#include "shared/console_id.h"
#include "sync/wifi_transport.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "ui/text.h"

namespace {

static const u32 kSocBufferSize = 0x100000;
static const int kMenuOptionCount = 2;
static const int kMenuButtonX = 5;
static const int kMenuButtonY0 = 70;
static const int kMenuButtonStride = 46;
static const int kMenuButtonW = 230;
static const int kMenuButtonH = 40;

void LayoutMenuButton(Button *button, int index) {
  button->Init();
  button->SetStyle(BUTTON_STYLE_SETTING);
  button->Resize(kMenuButtonW, kMenuButtonH);
  button->Move(kMenuButtonX, kMenuButtonY0 + index * kMenuButtonStride);
}

bool AskPairingCode(std::string *out) {
  SwkbdState swkbd;
  char buf[8] = {0};
  swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 4);
  swkbdSetHintText(&swkbd, "Pairing code shown on the host");
  swkbdSetValidation(&swkbd, SWKBD_FIXEDLEN, 0, 0);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Join", true);
  if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
    return false;
  *out = buf;
  return out->size() == 4;
}

} // namespace

SyncController::SyncController(App &app)
    : app_(app), screen_(kMenu), menu_index_(0), dirty_(true), last_phase_(-1),
      soc_buffer_(NULL), soc_ready_(false) {}

SyncController::~SyncController() {
  EndSession();
  StopNetwork();
}

std::string SyncController::DeviceName() const {
  // No user-visible console name without another system service; the end
  // of the console id tells two consoles apart.
  char name[32];
  snprintf(name, sizeof(name), "3DS %04X",
           (unsigned)(console_id::Get() & 0xFFFF));
  return name;
}

bool SyncController::StartNetwork(uint32_t *local_ip) {
  if (!soc_ready_) {
    soc_buffer_ = (u32 *)memalign(0x1000, kSocBufferSize);
    if (!soc_buffer_)
      return false;
    if (R_FAILED(socInit(soc_buffer_, kSocBufferSize))) {
      free(soc_buffer_);
      soc_buffer_ = NULL;
      return false;
    }
    soc_ready_ = true;
  }
  // gethostid() is this console's address (network byte order), or 0 when
  // it isn't connected to a network.
  const uint32_t ip = ntohl((uint32_t)gethostid());
  if (local_ip)
    *local_ip = ip;
  return ip != 0;
}

void SyncController::StopNetwork() {
  if (!soc_ready_)
    return;
  socExit();
  free(soc_buffer_);
  soc_buffer_ = NULL;
  soc_ready_ = false;
}

void SyncController::EndSession() {
  if (session_)
    session_->Cancel();
  session_.reset();
  if (transport_)
    transport_->Close();
  transport_.reset();
  last_phase_ = -1;
}

void SyncController::ShowError(const std::string &message) {
  app_.PrintStatus("SYNC failed: " + message);
  EndSession();
  StopNetwork();
  message_ = message;
  screen_ = kError;
  dirty_ = true;
}

sync_manifest::Manifest SyncController::BuildLocalManifest() {
  // Make sure the open book's position is current before it is shared.
  Book *current = app_.GetCurrentBook();
  if (current)
    current->SaveReadingProgress();

  sync_manifest::Manifest manifest;
  books_by_sync_id_.clear();
  for (size_t i = 0; i < app_.books.size(); i++) {
    Book *book = app_.books[i];
    if (!book || book->IsBrowserFolder() || !book->GetFileName())
      continue;
    const std::string path =
        std::string(book->GetFolderName() ? book->GetFolderName() : "") +
        "/" + book->GetFileName();
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
      continue;
    sync_manifest::BookEntry entry;
    entry.file_name = book->GetFileName();
    entry.file_size = (uint64_t)st.st_size;
    entry.state = book->GetBookState();
    books_by_sync_id_.push_back(std::make_pair(entry.SyncId(), book));
    manifest.books.push_back(entry);
  }
  return manifest;
}

void SyncController::StartHost() {
  uint32_t ip = 0;
  if (!StartNetwork(&ip)) {
    ShowError("Not connected to Wi-Fi. Connect in System Settings, then "
              "try again.");
    return;
  }
  char code[8];
  snprintf(code, sizeof(code), "%04u",
           (unsigned)(console_id::Generate(osGetTime() ^ svcGetSystemTick()) %
                      10000u));
  pairing_code_ = code;
  WifiTransport::Options options;
  options.local_ip = ip;
  transport_.reset(WifiTransport::CreateHost(DeviceName(), options));
  session_.reset(new SyncSession(pairing_code_, console_id::Get(),
                                 DeviceName(), BuildLocalManifest(),
                                 transport_.get()));
  screen_ = kHosting;
  dirty_ = true;
}

void SyncController::StartJoin() {
  std::string code;
  const bool entered = AskPairingCode(&code);
  // The keyboard applet replaced both screens.
  app_.ts->MarkAllScreensDirty();
  dirty_ = true;
  if (!entered)
    return;
  uint32_t ip = 0;
  if (!StartNetwork(&ip)) {
    ShowError("Not connected to Wi-Fi. Connect in System Settings, then "
              "try again.");
    return;
  }
  pairing_code_ = code;
  WifiTransport::Options options;
  options.local_ip = ip;
  transport_.reset(WifiTransport::CreateJoin(DeviceName(), options));
  session_.reset(new SyncSession(pairing_code_, console_id::Get(),
                                 DeviceName(), BuildLocalManifest(),
                                 transport_.get()));
  screen_ = kJoining;
}

void SyncController::ApplyResults() {
  int highlights_added = 0, records_updated = 0, positions = 0;
  const std::vector<SyncSession::BookResult> &results = session_->results();
  for (size_t i = 0; i < results.size(); i++) {
    const SyncSession::BookResult &r = results[i];
    for (size_t b = 0; b < books_by_sync_id_.size(); b++) {
      if (books_by_sync_id_[b].first != r.sync_id)
        continue;
      books_by_sync_id_[b].second->ApplySyncedState(r.merged);
      break;
    }
    highlights_added += r.stats.records_added;
    records_updated += r.stats.records_updated;
    if (r.stats.progress_changed)
      positions++;
  }
  if (!results.empty())
    app_.PersistPrefs();

  int only_remote = 0;
  const sync_manifest::Manifest &remote = session_->remote_manifest();
  for (size_t i = 0; i < remote.books.size(); i++) {
    bool found = false;
    for (size_t b = 0; b < books_by_sync_id_.size() && !found; b++)
      found = books_by_sync_id_[b].first == remote.books[i].SyncId();
    if (!found)
      only_remote++;
  }

  char line[96];
  snprintf(line, sizeof(line),
           "SYNC done with %s: matched=%d changed=%d added=%d updated=%d "
           "positions=%d",
           session_->peer_name().c_str(), session_->matched_books(),
           (int)results.size(), highlights_added, records_updated, positions);
  app_.PrintStatus(line);
  summary_lines_.clear();
  summary_lines_.push_back("Synced with " + session_->peer_name());
  snprintf(line, sizeof(line), "%d book%s on both consoles",
           session_->matched_books(),
           session_->matched_books() == 1 ? "" : "s");
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "%d book%s updated here", (int)results.size(),
           results.size() == 1 ? "" : "s");
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d new highlights/bookmarks",
           highlights_added);
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d changed or removed", records_updated);
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d reading position%s moved", positions,
           positions == 1 ? "" : "s");
  summary_lines_.push_back(line);
  if (only_remote > 0) {
    snprintf(line, sizeof(line), "%d book%s only on the other 3DS",
             only_remote, only_remote == 1 ? "" : "s");
    summary_lines_.push_back(line);
  }
}

void SyncController::Show() {
  EndSession();
  screen_ = kMenu;
  menu_index_ = 0;
  dirty_ = true;
  app_.buttonback.Move(screen_layout::kFooterMidX, screen_layout::kFooterY);
  app_.buttonback.Resize(screen_layout::kFooterMidW,
                         screen_layout::kFooterButtonH);
  app_.buttonback.Label("back");
  app_.buttonback.SetIcon(UI_BUTTON_ICON_BACK);
  app_.ts->SetScreen(app_.ts->screenright);
  app_.ts->MarkScreenDirty(app_.ts->screenright);
}

void SyncController::Leave() {
  EndSession();
  StopNetwork();
  app_.ShowSettingsView(false);
}

void SyncController::OnAppletSuspended() {
  if (screen_ == kHosting || screen_ == kJoining || screen_ == kSyncing)
    ShowError("Sync interrupted (HOME or sleep). Try again.");
}

void SyncController::RunFrame(const FrameInput &input) {
  const u32 keys = input.keys_down;
  touchPosition touch = {};
  const bool touched = (keys & KEY_TOUCH) != 0;
  if (touched)
    touch = app_.MapTouch(input);
  const bool back_touched =
      touched && app_.buttonback.EnclosesPoint(touch.px, touch.py);

  switch (screen_) {
  case kMenu: {
    int chosen = -1;
    if (keys & (KEY_DUP | KEY_CPAD_UP)) {
      menu_index_ = (menu_index_ + kMenuOptionCount - 1) % kMenuOptionCount;
      dirty_ = true;
    } else if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) {
      menu_index_ = (menu_index_ + 1) % kMenuOptionCount;
      dirty_ = true;
    } else if (keys & KEY_A) {
      chosen = menu_index_;
    } else if ((keys & KEY_B) || back_touched) {
      Leave();
      return;
    } else if (touched) {
      for (int i = 0; i < kMenuOptionCount; i++) {
        Button button(app_.ts.get());
        LayoutMenuButton(&button, i);
        if (button.EnclosesPoint(touch.px, touch.py))
          chosen = i;
      }
    }
    if (chosen == 0)
      StartHost();
    else if (chosen == 1)
      StartJoin();
    break;
  }
  case kHosting:
  case kJoining:
  case kSyncing: {
    if ((keys & KEY_B) || back_touched) {
      EndSession();
      StopNetwork();
      screen_ = kMenu;
      dirty_ = true;
      break;
    }
    if (!session_)
      break;
    session_->Poll(osGetTime());
    const SyncSession::Phase phase = session_->phase();
    if ((int)phase != last_phase_) {
      last_phase_ = (int)phase;
      dirty_ = true;
      if (phase == SyncSession::kExchanging ||
          phase == SyncSession::kFinishing)
        screen_ = kSyncing;
    }
    if (phase == SyncSession::kDone) {
      ApplyResults();
      EndSession();
      StopNetwork();
      screen_ = kSummary;
      dirty_ = true;
    } else if (phase == SyncSession::kFailed) {
      ShowError(session_->error());
    }
    break;
  }
  case kSummary:
  case kError:
    if ((keys & (KEY_A | KEY_B)) || back_touched) {
      screen_ = kMenu;
      dirty_ = true;
    }
    break;
  }

  if (dirty_)
    Draw();
}

void SyncController::Draw() {
  dirty_ = false;
  Text *ts = app_.ts.get();
  ts->SetScreen(ts->screenright);
  ts->ClearScreen();
  app_.DrawBottomGradientBackground();
  const int saved_style = ts->GetStyle();
  ts->SetStyle(TEXT_STYLE_BROWSER);
  const int row_h = ts->GetHeight() + 4;

  int y = 18;
  ts->SetPen(8, y);
  ts->PrintString("sync with another 3DS");
  y += row_h + 6;

  std::vector<std::string> lines;
  switch (screen_) {
  case kMenu:
    lines.push_back("Both consoles: same Wi-Fi, this screen open.");
    break;
  case kHosting:
    lines.push_back("This 3DS: " + DeviceName());
    lines.push_back("Pairing code:");
    lines.push_back("");
    lines.push_back("        " + pairing_code_);
    lines.push_back("");
    lines.push_back("On the other 3DS choose \"join\"");
    lines.push_back("and enter this code.");
    lines.push_back("");
    lines.push_back("Waiting...   B: cancel");
    break;
  case kJoining:
    lines.push_back("This 3DS: " + DeviceName());
    lines.push_back("Looking for the host on this Wi-Fi");
    lines.push_back("network...");
    lines.push_back("");
    lines.push_back("B: cancel");
    break;
  case kSyncing:
    lines.push_back("Connected" +
                    (session_ && !session_->peer_name().empty()
                         ? " to " + session_->peer_name()
                         : std::string()));
    lines.push_back("Exchanging progress, bookmarks");
    lines.push_back("and highlights...");
    break;
  case kSummary:
    lines = summary_lines_;
    lines.push_back("");
    lines.push_back("A: done");
    break;
  case kError:
    lines.push_back("Sync failed:");
    lines.push_back(message_);
    lines.push_back("");
    lines.push_back("A: back");
    break;
  }
  for (size_t i = 0; i < lines.size(); i++) {
    // Wrap long lines (e.g. error messages) at the screen width.
    std::string rest = lines[i];
    do {
      std::string part = rest;
      while (part.size() > 1 &&
             ts->GetStringWidth(part.c_str(), TEXT_STYLE_BROWSER) > 228) {
        const size_t cut = part.rfind(' ', part.size() - 2);
        part = part.substr(0, cut == std::string::npos ? part.size() - 1
                                                       : cut);
      }
      ts->SetPen(8, y);
      ts->PrintString(part.c_str());
      y += row_h;
      rest = rest.size() > part.size() ? rest.substr(part.size()) : "";
      while (!rest.empty() && rest[0] == ' ')
        rest.erase(0, 1);
    } while (!rest.empty());
  }

  if (screen_ == kMenu) {
    static const char *kLabels[kMenuOptionCount] = {"host a sync",
                                                    "join a sync"};
    static const char *kHints[kMenuOptionCount] = {
        "show a pairing code here >", "enter the host's code >"};
    for (int i = 0; i < kMenuOptionCount; i++) {
      Button button(ts);
      LayoutMenuButton(&button, i);
      button.SetLabel1(kLabels[i]);
      button.SetLabel2(kHints[i]);
      button.Draw(ts->screenright, i == menu_index_);
    }
  }
  app_.buttonback.Draw(ts->screenright, false);
  ts->SetStyle(saved_style);
  ts->MarkScreenDirty(ts->screenright);
}
