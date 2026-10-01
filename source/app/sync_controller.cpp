/*
    3dslibris - sync_controller.cpp

    The "sync with another 3DS" screen. See include/app/sync_controller.h.
*/

#include "app/sync_controller.h"

#include <3ds.h>
#include <algorithm>
#include <arpa/inet.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/app.h"
#include "app/library_files.h"
#include "app/frame_input.h"
#include "app/hardcover_controller.h"
#include "book/annotation_store_utils.h"
#include "book/book.h"
#include "book/hardcover_utils.h"
#include "settings/prefs.h"
#include "shared/console_id.h"
#include "shared/path_constants.h"
#include "sync/uds_transport.h"
#include "sync/wifi_transport.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "ui/text.h"

namespace {

static const u32 kSocBufferSize = 0x100000;
// host, join, connection type
static const int kMenuOptionCount = 3;
static const int kMenuButtonX = 5;
static const int kMenuButtonY0 = 70;
static const int kMenuButtonStride = 46;
static const int kMenuButtonW = 230;
static const int kMenuButtonH = 40;
static const int kTextX = 8;
static const int kTextRight = 236;
// Picker list and the key hints under it.
static const int kPickListY = 64;
static const int kPickHintsY = 252;
// Redraw the copy progress at most this often (drawing costs copy time).
static const uint64_t kProgressRedrawMs = 250;
// Rough copy speeds on Old 3DS, for the time estimate.
static const uint64_t kWifiBytesPerSec = 400 * 1024;
static const uint64_t kLocalBytesPerSec = 100 * 1024;

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

uint64_t SdFreeBytes(void *) {
  FS_ArchiveResource res;
  if (R_FAILED(FSUSER_GetArchiveResource(&res, SYSTEM_MEDIATYPE_SD)))
    return UINT64_MAX; // unknown: let the write fail if it must
  return (uint64_t)res.freeClusters * res.clusterSize;
}

std::string StatePath(const std::string &folder, const std::string &file) {
  return paths::GetAnnotationsDir() + "/" +
         annotation_store_utils::BuildFileName(folder, file);
}

std::vector<uint16_t> LiveBookmarkPages(const BookState &state) {
  std::vector<uint16_t> pages;
  for (size_t i = 0; i < state.records.size(); i++)
    if (state.records[i].IsLiveBookmark())
      pages.push_back(state.records[i].page_hint);
  std::sort(pages.begin(), pages.end());
  pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
  return pages;
}

// For a book that isn't loaded: the state file, plus the page numbers prefs
// keep for the library and for older versions.
void SaveUnloadedState(App &app, const std::string &folder,
                       const std::string &file, const BookState &state) {
  annotation_store_utils::SaveFile(StatePath(folder, file), state);
  if (app.prefs)
    app.prefs->ApplySyncedBookPages(
        folder.c_str(), file.c_str(),
        state.has_progress ? (int)state.progress.page_hint : -1,
        LiveBookmarkPages(state));
}

std::string FormatSize(uint64_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024)
    snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
  else
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)((bytes + 1023) / 1024));
  return buf;
}

std::string FormatDuration(uint64_t seconds) {
  char buf[32];
  if (seconds < 60)
    snprintf(buf, sizeof(buf), "%u s", (unsigned)(seconds ? seconds : 1));
  else
    snprintf(buf, sizeof(buf), "%u min", (unsigned)((seconds + 59) / 60));
  return buf;
}

size_t NextUtf8(const std::string &s, size_t i) {
  i++;
  while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80)
    i++;
  return i;
}

// Width of s in pixels. Text::GetStringWidth returns a u8, so it is only
// trusted for short pieces; longer strings are measured in parts.
int MeasureWidth(Text *ts, const std::string &s) {
  int width = 0;
  size_t start = 0;
  while (start < s.size()) {
    size_t end = start;
    for (int n = 0; n < 8 && end < s.size(); n++)
      end = NextUtf8(s, end);
    width += ts->GetStringWidth(s.substr(start, end - start).c_str(),
                                TEXT_STYLE_BROWSER);
    start = end;
  }
  return width;
}

// The longest prefix of s (whole characters) that fits in max_px.
size_t FittingPrefix(Text *ts, const std::string &s, int max_px) {
  size_t fit = 0;
  int width = 0;
  for (size_t i = 0; i < s.size();) {
    const size_t next = NextUtf8(s, i);
    width += ts->GetStringWidth(s.substr(i, next - i).c_str(),
                                TEXT_STYLE_BROWSER);
    if (width > max_px)
      break;
    fit = next;
    i = next;
  }
  return fit;
}

std::string Ellipsize(Text *ts, const std::string &s, int max_px) {
  if (MeasureWidth(ts, s) <= max_px)
    return s;
  const int dots = MeasureWidth(ts, "...");
  return s.substr(0, FittingPrefix(ts, s, max_px - dots)) + "...";
}

} // namespace

SyncController::SyncController(App &app)
    : app_(app), screen_(kMenu), menu_index_(0), dirty_(true), last_phase_(-1),
      results_applied_(false), received_saved_(false), last_draw_ms_(0),
      last_books_sent_(0), changed_books_(0), records_added_(0),
      records_updated_(0), positions_moved_(0), links_changed_(0), pick_cursor_(0), pick_top_(0),
      local_wireless_(false), soc_buffer_(NULL), soc_ready_(false) {}

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
  // A cancelled copy keeps its .part file for the next sync.
  sink_.reset();
  source_.reset();
  library_.clear();
  missing_.clear();
  picked_.clear();
  last_phase_ = -1;
}

void SyncController::ShowError(const std::string &message) {
  app_.PrintStatus("SYNC failed: " + message);
  // Books that arrived before the failure are kept.
  SaveReceivedBooks();
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

  const std::vector<sync_book_files::LocalBook> files =
      library_files::ScanAll(app_);

  sync_manifest::Manifest manifest;
  library_.clear();
  for (size_t i = 0; i < files.size(); i++) {
    LibraryItem item;
    item.file = files[i];
    item.loaded = NULL;
    // Books in the current library view (or open) carry newer state than
    // their files.
    for (size_t b = 0; b < app_.books.size() && !item.loaded; b++) {
      Book *book = app_.books[b];
      if (book && !book->IsBrowserFolder() && book->GetFileName() &&
          book->GetFolderName() && item.file.file_name == book->GetFileName() &&
          item.file.folder == book->GetFolderName())
        item.loaded = book;
    }
    if (!item.loaded && current && current->GetFileName() &&
        current->GetFolderName() &&
        item.file.file_name == current->GetFileName() &&
        item.file.folder == current->GetFolderName())
      item.loaded = current;

    sync_manifest::BookEntry entry;
    entry.file_name = item.file.file_name;
    entry.file_size = item.file.size;
    if (item.loaded) {
      entry.state = item.loaded->GetBookState();
    } else if (!annotation_store_utils::LoadFile(
                   StatePath(item.file.folder, item.file.file_name),
                   console_id::Prefix(), &entry.state)) {
      entry.state = BookState();
    }
    library_.push_back(item);
    manifest.books.push_back(entry);
  }
  manifest.hardcover_links = HardcoverController::LoadLinksText();
  return manifest;
}

void SyncController::StartSession() {
  std::string dest = app_.bookdir;
  if (dest.compare(0, 5, "sdmc:") != 0)
    dest = paths::GetBookDir();
  mkdir(dest.c_str(), 0777);
  const sync_manifest::Manifest manifest = BuildLocalManifest();
  std::vector<sync_book_files::LocalBook> files;
  for (size_t i = 0; i < library_.size(); i++)
    files.push_back(library_[i].file);
  source_.reset(new FileBookSource(files));
  sink_.reset(new FileBookSink(dest, &library_files::IsLibraryBook,
                               &SdFreeBytes, NULL));
  session_.reset(new SyncSession(pairing_code_, console_id::Get(),
                                 DeviceName(), manifest, transport_.get(),
                                 source_.get(), sink_.get()));
  results_applied_ = false;
  received_saved_ = false;
  last_books_sent_ = 0;
  changed_books_ = records_added_ = records_updated_ = positions_moved_ = 0;
  links_changed_ = 0;
}

bool SyncController::OpenTransport(bool host) {
  if (local_wireless_) {
    transport_.reset(host ? UdsTransport::CreateHost(DeviceName(),
                                                     pairing_code_)
                          : UdsTransport::CreateJoin(DeviceName(),
                                                     pairing_code_));
    if (transport_->GetState() == SyncTransport::kFailed) {
      ShowError(transport_->Error());
      return false;
    }
    return true;
  }
  uint32_t ip = 0;
  if (!StartNetwork(&ip)) {
    ShowError("Not connected to Wi-Fi. Connect in System Settings, or "
              "choose local wireless, then try again.");
    return false;
  }
  WifiTransport::Options options;
  options.local_ip = ip;
  transport_.reset(host ? WifiTransport::CreateHost(DeviceName(), options)
                        : WifiTransport::CreateJoin(DeviceName(), options));
  return true;
}

void SyncController::StartHost() {
  char code[8];
  snprintf(code, sizeof(code), "%04u",
           (unsigned)(console_id::Generate(osGetTime() ^ svcGetSystemTick()) %
                      10000u));
  pairing_code_ = code;
  if (!OpenTransport(true))
    return;
  StartSession();
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
  pairing_code_ = code;
  if (!OpenTransport(false))
    return;
  StartSession();
  screen_ = kJoining;
}

void SyncController::ApplyResults() {
  results_applied_ = true;
  const std::vector<SyncSession::BookResult> &results = session_->results();
  for (size_t i = 0; i < results.size(); i++) {
    const SyncSession::BookResult &r = results[i];
    for (size_t b = 0; b < library_.size(); b++) {
      if (library_[b].file.SyncId() != r.sync_id)
        continue;
      if (library_[b].loaded)
        library_[b].loaded->ApplySyncedState(r.merged);
      else
        SaveUnloadedState(app_, library_[b].file.folder,
                          library_[b].file.file_name, r.merged);
      break;
    }
    records_added_ += r.stats.records_added;
    records_updated_ += r.stats.records_updated;
    if (r.stats.progress_changed)
      positions_moved_++;
  }
  changed_books_ = (int)results.size();
  if (!results.empty())
    app_.PersistPrefs();

  // Hardcover links, for every book (also ones not on this console yet).
  const std::string links_text = HardcoverController::LoadLinksText();
  std::vector<hardcover_utils::Link> links =
      hardcover_utils::ParseLinks(links_text);
  links_changed_ = hardcover_utils::MergeLinks(
      &links,
      hardcover_utils::ParseLinks(session_->remote_manifest().hardcover_links));
  const std::string merged_links = hardcover_utils::SerializeLinks(links);
  if (!links.empty() && merged_links != links_text)
    HardcoverController::SaveLinksText(merged_links);

  char line[112];
  snprintf(line, sizeof(line),
           "SYNC merged with %s: matched=%d changed=%d added=%d updated=%d "
           "positions=%d links=%d missing=%d",
           session_->peer_name().c_str(), session_->matched_books(),
           changed_books_, records_added_, records_updated_, positions_moved_,
           links_changed_,
           (int)session_->MissingBooks().size());
  app_.PrintStatus(line);
}

void SyncController::SaveReceivedBooks() {
  if (!session_ || received_saved_ || !sink_)
    return;
  received_saved_ = true;
  const std::vector<const sync_manifest::BookEntry *> &received =
      session_->books_received();
  if (received.empty())
    return;
  for (size_t i = 0; i < received.size(); i++) {
    const sync_manifest::BookEntry *book = received[i];
    if (!book->state.records.empty() || book->state.has_progress)
      SaveUnloadedState(app_, sink_->dest_dir(), book->file_name, book->state);
    app_.PrintStatus("SYNC received " + book->file_name);
  }
  app_.PersistPrefs();
  app_.RequestLibraryRescan();
}

void SyncController::BuildSummary() {
  char line[96];
  summary_lines_.clear();
  summary_lines_.push_back("Synced with " + session_->peer_name());
  snprintf(line, sizeof(line), "%d book%s on both consoles",
           session_->matched_books(),
           session_->matched_books() == 1 ? "" : "s");
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "%d book%s updated here", changed_books_,
           changed_books_ == 1 ? "" : "s");
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d new highlights/bookmarks", records_added_);
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d changed or removed", records_updated_);
  summary_lines_.push_back(line);
  snprintf(line, sizeof(line), "  %d reading position%s moved",
           positions_moved_, positions_moved_ == 1 ? "" : "s");
  summary_lines_.push_back(line);
  if (links_changed_ > 0) {
    snprintf(line, sizeof(line), "%d Hardcover link%s updated",
             links_changed_, links_changed_ == 1 ? "" : "s");
    summary_lines_.push_back(line);
  }

  const int received = (int)session_->books_received().size();
  const std::vector<std::string> &failures = session_->book_failures();
  if (received > 0) {
    snprintf(line, sizeof(line), "%d book%s copied here", received,
             received == 1 ? "" : "s");
    summary_lines_.push_back(line);
  }
  if (session_->books_sent() > 0) {
    snprintf(line, sizeof(line), "%d book%s sent to the other 3DS",
             session_->books_sent(), session_->books_sent() == 1 ? "" : "s");
    summary_lines_.push_back(line);
  }
  if (!failures.empty()) {
    snprintf(line, sizeof(line), "%d book%s not copied:", (int)failures.size(),
             failures.size() == 1 ? "" : "s");
    summary_lines_.push_back(line);
    for (size_t i = 0; i < failures.size() && i < 3; i++) {
      summary_lines_.push_back("  " + failures[i]);
      app_.PrintStatus("SYNC not copied: " + failures[i]);
    }
    if (failures.size() > 3)
      summary_lines_.push_back("  ...");
  }
  const int missing = (int)session_->MissingBooks().size() - received -
                      (int)failures.size();
  if (missing > 0) {
    snprintf(line, sizeof(line), "%d book%s only on the other 3DS", missing,
             missing == 1 ? "" : "s");
    summary_lines_.push_back(line);
  }
}

void SyncController::EnterPicker() {
  // A different file with the same name (e.g. another edition) can't be
  // saved next to ours, so it isn't offered.
  missing_.clear();
  const std::vector<const sync_manifest::BookEntry *> missing =
      session_->MissingBooks();
  for (size_t i = 0; i < missing.size(); i++) {
    bool clash = false;
    for (size_t b = 0; b < library_.size() && !clash; b++)
      clash = library_[b].file.file_name == missing[i]->file_name;
    if (clash)
      app_.PrintStatus("SYNC not offered (name in use): " +
                       missing[i]->file_name);
    else
      missing_.push_back(missing[i]);
  }
  if (missing_.empty()) {
    session_->SkipBooks();
    screen_ = kSyncing;
    return;
  }
  picked_.assign(missing_.size(), false);
  pick_cursor_ = 0;
  pick_top_ = 0;
  screen_ = kPicking;
}

int SyncController::PickerVisibleRows() const {
  const int row_h = app_.ts->GetHeight() + 4;
  const int rows = (kPickHintsY - kPickListY) / row_h;
  return rows > 1 ? rows : 1;
}

void SyncController::RunPicker(uint32_t keys, bool touched, int touch_x,
                               int touch_y) {
  const int count = (int)missing_.size();
  const int visible = PickerVisibleRows();
  if (keys & (KEY_DUP | KEY_CPAD_UP)) {
    pick_cursor_ = (pick_cursor_ + count - 1) % count;
    dirty_ = true;
  } else if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) {
    pick_cursor_ = (pick_cursor_ + 1) % count;
    dirty_ = true;
  } else if (keys & (KEY_DLEFT | KEY_L)) {
    pick_cursor_ = std::max(0, pick_cursor_ - visible);
    dirty_ = true;
  } else if (keys & (KEY_DRIGHT | KEY_R)) {
    pick_cursor_ = std::min(count - 1, pick_cursor_ + visible);
    dirty_ = true;
  } else if (keys & KEY_A) {
    picked_[pick_cursor_] = !picked_[pick_cursor_];
    dirty_ = true;
  } else if (keys & KEY_Y) {
    // Select all, or clear if everything is already selected.
    const bool all =
        std::find(picked_.begin(), picked_.end(), false) == picked_.end();
    picked_.assign(picked_.size(), !all);
    dirty_ = true;
  } else if (keys & (KEY_START | KEY_X)) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < missing_.size(); i++)
      if (picked_[i])
        ids.push_back(missing_[i]->SyncId());
    session_->RequestBooks(ids);
    screen_ = ids.empty() ? kSyncing : kTransferring;
    dirty_ = true;
    return;
  } else if (keys & KEY_B) {
    session_->SkipBooks();
    screen_ = kSyncing;
    dirty_ = true;
    return;
  } else if (touched && touch_y >= kPickListY && touch_y < kPickHintsY) {
    const int row = pick_top_ + (touch_y - kPickListY) /
                                    (app_.ts->GetHeight() + 4);
    if (row < count && touch_x < kTextRight) {
      pick_cursor_ = row;
      picked_[row] = !picked_[row];
      dirty_ = true;
    }
  }
  if (pick_cursor_ < pick_top_)
    pick_top_ = pick_cursor_;
  if (pick_cursor_ >= pick_top_ + visible)
    pick_top_ = pick_cursor_ - visible + 1;
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
  if (screen_ == kHosting || screen_ == kJoining || screen_ == kSyncing ||
      screen_ == kPicking || screen_ == kTransferring)
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
    if (chosen == 0) {
      StartHost();
    } else if (chosen == 1) {
      StartJoin();
    } else if (chosen == 2) {
      local_wireless_ = !local_wireless_;
      dirty_ = true;
    }
    break;
  }
  case kHosting:
  case kJoining:
  case kSyncing:
  case kPicking:
  case kTransferring: {
    // In the picker B means "copy nothing"; elsewhere it cancels.
    if (screen_ == kPicking && session_) {
      RunPicker(back_touched ? (u32)KEY_B : keys, touched && !back_touched,
                touch.px, touch.py);
    } else if ((keys & KEY_B) || back_touched) {
      app_.PrintStatus("SYNC cancelled");
      SaveReceivedBooks();
      EndSession();
      StopNetwork();
      screen_ = kMenu;
      dirty_ = true;
      break;
    }
    if (!session_)
      break;
    const uint64_t now = osGetTime();
    session_->Poll(now);
    const SyncSession::Phase phase = session_->phase();
    if ((int)phase != last_phase_) {
      last_phase_ = (int)phase;
      dirty_ = true;
      if (phase == SyncSession::kExchanging)
        screen_ = kSyncing;
      if (phase >= SyncSession::kChoosing && phase != SyncSession::kFailed &&
          !results_applied_)
        ApplyResults();
      if (phase == SyncSession::kChoosing)
        EnterPicker();
      else if (phase == SyncSession::kTransferring)
        screen_ = kTransferring;
      else if (phase == SyncSession::kFinishing)
        screen_ = kSyncing;
    }
    if (screen_ == kTransferring && now - last_draw_ms_ >= kProgressRedrawMs)
      dirty_ = true;
    if (session_->books_sent() != last_books_sent_) {
      last_books_sent_ = session_->books_sent();
      dirty_ = true;
    }
    if (phase == SyncSession::kDone) {
      SaveReceivedBooks();
      BuildSummary();
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

void SyncController::DrawPicker(int y) {
  Text *ts = app_.ts.get();
  const int row_h = ts->GetHeight() + 4;
  uint64_t chosen_bytes = 0;
  int chosen = 0;
  for (size_t i = 0; i < missing_.size(); i++) {
    if (picked_[i]) {
      chosen_bytes += missing_[i]->file_size;
      chosen++;
    }
  }
  char line[96];
  if (chosen > 0)
    snprintf(line, sizeof(line), "%d of %d: %s (~%s)", chosen,
             (int)missing_.size(), FormatSize(chosen_bytes).c_str(),
             FormatDuration(chosen_bytes / (local_wireless_
                                                ? kLocalBytesPerSec
                                                : kWifiBytesPerSec))
                 .c_str());
  else
    snprintf(line, sizeof(line), "%d book%s only on the other 3DS",
             (int)missing_.size(), missing_.size() == 1 ? "" : "s");
  ts->SetPen(kTextX, y);
  ts->PrintString(line);

  const int visible = PickerVisibleRows();
  for (int r = 0; r < visible; r++) {
    const int i = pick_top_ + r;
    if (i >= (int)missing_.size())
      break;
    const int row_y = kPickListY + r * row_h;
    const std::string size = FormatSize(missing_[i]->file_size);
    const int size_w = MeasureWidth(ts, size);
    const std::string mark = std::string(i == pick_cursor_ ? ">" : " ") +
                             (picked_[i] ? "[x] " : "[ ] ");
    const int mark_w = MeasureWidth(ts, mark);
    ts->SetPen(kTextX - 4, row_y + ts->GetHeight());
    ts->PrintString(mark.c_str());
    const std::string name =
        Ellipsize(ts, missing_[i]->file_name,
                  kTextRight - size_w - 8 - (kTextX - 4 + mark_w));
    ts->SetPen(kTextX - 4 + mark_w, row_y + ts->GetHeight());
    ts->PrintString(name.c_str());
    ts->SetPen(kTextRight - size_w, row_y + ts->GetHeight());
    ts->PrintString(size.c_str());
  }
  if (pick_top_ > 0 || pick_top_ + visible < (int)missing_.size()) {
    snprintf(line, sizeof(line), "%d-%d of %d", pick_top_ + 1,
             std::min(pick_top_ + visible, (int)missing_.size()),
             (int)missing_.size());
    ts->SetPen(kTextRight - MeasureWidth(ts, line), kPickListY - 2);
    ts->PrintString(line);
  }
  ts->SetPen(kTextX, kPickHintsY + ts->GetHeight());
  ts->PrintString("A: choose  Y: all  START: copy");
  ts->SetPen(kTextX, kPickHintsY + ts->GetHeight() + row_h);
  ts->PrintString("B: don't copy books");
}

void SyncController::DrawTransfer(int y) {
  Text *ts = app_.ts.get();
  const int row_h = ts->GetHeight() + 4;
  SyncSession::TransferProgress progress;
  if (!session_ || !session_->GetTransferProgress(&progress)) {
    ts->SetPen(kTextX, y);
    ts->PrintString("Copying books...");
    return;
  }
  char line[96];
  snprintf(line, sizeof(line), "Copying book %d of %d", progress.index,
           progress.count);
  ts->SetPen(kTextX, y);
  ts->PrintString(line);
  y += row_h;
  ts->SetPen(kTextX, y);
  ts->PrintString(
      Ellipsize(ts, progress.file_name, kTextRight - kTextX).c_str());
  y += row_h + 6;
  const u16 color = ts->GetFgColor();
  const int bar_x0 = kTextX, bar_x1 = kTextRight;
  const int bar_y0 = y - ts->GetHeight(), bar_y1 = bar_y0 + 12;
  ts->DrawRect(bar_x0, bar_y0, bar_x1, bar_y1, color);
  if (progress.total > 0) {
    const int fill = (int)((uint64_t)(bar_x1 - bar_x0 - 4) *
                           progress.received / progress.total);
    if (fill > 0)
      ts->FillRect(bar_x0 + 2, bar_y0 + 2, bar_x0 + 2 + fill, bar_y1 - 2,
                   color);
  }
  y += 12 + row_h;
  snprintf(line, sizeof(line), "%s of %s",
           FormatSize(progress.received).c_str(),
           FormatSize(progress.total).c_str());
  ts->SetPen(kTextX, y);
  ts->PrintString(line);
  y += row_h * 2;
  ts->SetPen(kTextX, y);
  ts->PrintString("B: stop (resumes next sync)");
}

void SyncController::Draw() {
  dirty_ = false;
  last_draw_ms_ = osGetTime();
  Text *ts = app_.ts.get();
  ts->SetScreen(ts->screenright);
  ts->ClearScreen();
  app_.DrawBottomGradientBackground();
  const int saved_style = ts->GetStyle();
  ts->SetStyle(TEXT_STYLE_BROWSER);
  const int row_h = ts->GetHeight() + 4;

  int y = 18;
  ts->SetPen(kTextX, y);
  ts->PrintString(screen_ == kPicking ? "copy books from the other 3DS"
                                      : "sync with another 3DS");
  y += row_h + 6;

  std::vector<std::string> lines;
  switch (screen_) {
  case kMenu:
    lines.push_back(local_wireless_
                        ? "Local wireless: no router needed. Keep the "
                          "consoles close, with this screen open."
                        : "Both consoles: same Wi-Fi, this screen open.");
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
    if (local_wireless_) {
      lines.push_back("Looking for a host nearby with");
      lines.push_back("code " + pairing_code_ + "...");
    } else {
      lines.push_back("Looking for the host on this Wi-Fi");
      lines.push_back("network...");
    }
    lines.push_back("");
    lines.push_back("B: cancel");
    break;
  case kSyncing: {
    lines.push_back("Connected" +
                    (session_ && !session_->peer_name().empty()
                         ? " to " + session_->peer_name()
                         : std::string()));
    const bool merged = session_ && session_->phase() >= SyncSession::kChoosing;
    if (!merged) {
      lines.push_back("Exchanging progress, bookmarks");
      lines.push_back("and highlights...");
    } else {
      lines.push_back("Progress, bookmarks and highlights");
      lines.push_back("are synced.");
      lines.push_back("");
      lines.push_back("Waiting for the other 3DS to");
      lines.push_back("choose or copy books...");
      if (session_->books_sent() > 0) {
        char line[64];
        snprintf(line, sizeof(line), "%d book%s sent",
                 session_->books_sent(),
                 session_->books_sent() == 1 ? "" : "s");
        lines.push_back(line);
      }
    }
    lines.push_back("");
    lines.push_back("B: cancel");
    break;
  }
  case kPicking:
    DrawPicker(y);
    break;
  case kTransferring:
    DrawTransfer(y);
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
  const int max_w = kTextRight - kTextX;
  for (size_t i = 0; i < lines.size(); i++) {
    // Wrap long lines (e.g. error messages) at the screen width.
    std::string rest = lines[i];
    do {
      size_t cut = rest.size();
      if (MeasureWidth(ts, rest) > max_w) {
        cut = FittingPrefix(ts, rest, max_w);
        const size_t space = rest.rfind(' ', cut);
        if (space != std::string::npos && space > 0)
          cut = space;
        if (cut == 0)
          cut = NextUtf8(rest, 0);
      }
      ts->SetPen(kTextX, y);
      ts->PrintString(rest.substr(0, cut).c_str());
      y += row_h;
      rest = rest.substr(cut);
      while (!rest.empty() && rest[0] == ' ')
        rest.erase(0, 1);
    } while (!rest.empty());
  }

  if (screen_ == kMenu) {
    const char *labels[kMenuOptionCount] = {
        "host a sync", "join a sync",
        local_wireless_ ? "connection: local wireless" : "connection: Wi-Fi"};
    const char *hints[kMenuOptionCount] = {"show a pairing code here >",
                                           "enter the host's code >",
                                           "both consoles must match >"};
    for (int i = 0; i < kMenuOptionCount; i++) {
      Button button(ts);
      LayoutMenuButton(&button, i);
      button.SetLabel1(labels[i]);
      button.SetLabel2(hints[i]);
      button.Draw(ts->screenright, i == menu_index_);
    }
  }
  app_.buttonback.Draw(ts->screenright, false);
  ts->SetStyle(saved_style);
  ts->MarkScreenDirty(ts->screenright);
}
