/*
    3dslibris - readwise_controller.cpp

    See include/app/readwise_controller.h.
*/

#include "app/readwise_controller.h"

#include <3ds.h>
#include <algorithm>
#include <stdio.h>
#include <sys/stat.h>

#include "app/app.h"
#include "app/frame_input.h"
#include "app/https_client.h"
#include "app/library_files.h"
#include "app/readwise_client.h"
#include "book/readwise_api_utils.h"
#include "book/readwise_export.h"
#include "shared/path_constants.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "ui/text.h"

namespace {

const int kButtonX = 5;
const int kButtonY0 = 106;
const int kButtonStride = 46;
const int kButtonW = 230;
const int kButtonH = 40;
const int kTextX = 8;
const int kTextWidth = 228;

const char kAuthUrl[] = "https://readwise.io/api/v2/auth/";

std::string TokenPath() { return paths::GetSdmcBase() + "/readwise-token.txt"; }
std::string LogPath() { return paths::GetSdmcBase() + "/readwise-uploaded.txt"; }

bool ReadFile(const std::string &path, std::string *out) {
  out->clear();
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp)
    return false;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    out->append(buf, n);
  fclose(fp);
  return true;
}

bool WriteFile(const std::string &path, const std::string &data) {
  const std::string tmp = path + ".tmp";
  FILE *fp = fopen(tmp.c_str(), "wb");
  if (!fp)
    return false;
  const bool wrote = fwrite(data.data(), 1, data.size(), fp) == data.size();
  if (fclose(fp) != 0 || !wrote) {
    remove(tmp.c_str());
    return false;
  }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

std::vector<std::string> AuthHeaders(const std::string &token) {
  std::vector<std::string> headers;
  headers.push_back("Authorization: Token " + token);
  return headers;
}

void LayoutButton(Button *button, int index) {
  button->Init();
  button->SetStyle(BUTTON_STYLE_SETTING);
  button->Resize(kButtonW, kButtonH);
  button->Move(kButtonX, kButtonY0 + index * kButtonStride);
}

// Text::GetStringWidth returns a u8, so measure a character at a time.
std::vector<std::string> Wrap(Text *ts, const std::string &text) {
  std::vector<std::string> lines;
  std::string rest = text;
  while (!rest.empty()) {
    size_t fit = 0;
    int width = 0;
    for (size_t i = 0; i < rest.size();) {
      size_t next = i + 1;
      while (next < rest.size() && ((unsigned char)rest[next] & 0xC0) == 0x80)
        next++;
      width += ts->GetStringWidth(rest.substr(i, next - i).c_str(),
                                  TEXT_STYLE_BROWSER);
      if (width > kTextWidth)
        break;
      fit = next;
      i = next;
    }
    if (fit == 0)
      fit = rest.size();
    if (fit < rest.size()) {
      const size_t space = rest.rfind(' ', fit);
      if (space != std::string::npos && space > 0)
        fit = space;
    }
    lines.push_back(rest.substr(0, fit));
    rest = rest.substr(fit);
    while (!rest.empty() && rest[0] == ' ')
      rest.erase(0, 1);
  }
  return lines;
}

bool AskToken(std::string *out) {
  SwkbdState swkbd;
  char buf[128] = {0};
  swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 2, 100);
  swkbdSetHintText(&swkbd, "Token from readwise.io/access_token");
  swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Save", true);
  if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
    return false;
  *out = readwise_api_utils::CleanToken(buf);
  return !out->empty();
}

std::string Plural(int n, const char *word) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d %s%s", n, word, n == 1 ? "" : "s");
  return buf;
}

} // namespace

ReadwiseController::ReadwiseController(App &app)
    : app_(app), index_(kUpload), dirty_(true), pending_(kNone) {}

std::string ReadwiseController::LoadToken() const {
  std::string raw;
  ReadFile(TokenPath(), &raw);
  return readwise_api_utils::CleanToken(raw);
}

bool ReadwiseController::SaveToken(const std::string &token) const {
  return WriteFile(TokenPath(), token + "\n");
}

void ReadwiseController::SetLines(const std::string &a, const std::string &b,
                                  const std::string &c) {
  lines_.clear();
  lines_.push_back(a);
  if (!b.empty())
    lines_.push_back(b);
  if (!c.empty())
    lines_.push_back(c);
  dirty_ = true;
}

void ReadwiseController::Show() {
  index_ = kUpload;
  pending_ = kNone;
  if (LoadToken().empty())
    SetLines("Set your Readwise token first (see", "\"Readwise token\").");
  else
    SetLines("Uploads send only highlights that are",
             "new or edited since the last upload.");
  app_.buttonback.Move(screen_layout::kFooterMidX, screen_layout::kFooterY);
  app_.buttonback.Resize(screen_layout::kFooterMidW,
                         screen_layout::kFooterButtonH);
  app_.buttonback.Label("back");
  app_.buttonback.SetIcon(UI_BUTTON_ICON_BACK);
  app_.ts->SetScreen(app_.ts->screenright);
  app_.ts->MarkScreenDirty(app_.ts->screenright);
}

void ReadwiseController::Leave() { app_.ShowSettingsView(false); }

void ReadwiseController::Start(Pending work, const char *message) {
  pending_ = work;
  SetLines(message, "This takes a few seconds...");
}

void ReadwiseController::Upload() {
  const std::string token = LoadToken();
  if (token.empty()) {
    SetLines("Set your Readwise token first.");
    return;
  }
  const library_files::HighlightSet set = library_files::CollectHighlights(app_);
  // Highlights sent by versions before the upload state was kept in the
  // book data are listed in this per-console log.
  std::string log_text;
  ReadFile(LogPath(), &log_text);
  const readwise_api_utils::Work work = readwise_api_utils::Classify(
      set.highlights, readwise_api_utils::ParseLog(log_text));
  if (work.create.empty() && work.update.empty()) {
    SetLines(set.highlights.empty()
                 ? std::string("No highlights yet.")
                 : "All " + Plural((int)set.highlights.size(), "highlight") +
                       " are already in Readwise.");
    return;
  }

  https_client::Session session;
  if (!session.ok() || !session.HasNetwork()) {
    SetLines("Not connected to Wi-Fi.");
    return;
  }
  readwise_client::Client client(session, token);
  std::vector<readwise_api_utils::Highlight> done;
  int created = 0, updated = 0;
  std::string failure;

  // New highlights, in batches (their color goes in as a tag).
  for (size_t start = 0; start < work.create.size() && failure.empty();
       start += readwise_api_utils::kBatchSize) {
    const size_t end =
        std::min(work.create.size(), start + readwise_api_utils::kBatchSize);
    std::vector<readwise_api_utils::Highlight> batch(
        work.create.begin() + start, work.create.begin() + end);
    if (!client.Create(batch)) {
      failure = client.error();
      break;
    }
    for (size_t i = 0; i < batch.size(); i++) {
      batch[i].readwise_uploaded = batch[i].modified;
      done.push_back(batch[i]);
    }
    created += (int)batch.size();
    // Kept after every batch, so a later failure doesn't resend these.
    library_files::SaveUploadState(app_, done);
    done.clear();
  }

  // Highlights already in Readwise whose note or color changed (or that
  // were sent before colors were): update them by Readwise's id.
  for (size_t i = 0; i < work.update.size() && failure.empty(); i++) {
    readwise_api_utils::Highlight h = work.update[i];
    uint64_t id = h.readwise_id;
    if (!id && !client.FindHighlightId(h, &id)) {
      failure = client.error();
      break;
    }
    if (!id) {
      // Not in Readwise after all (e.g. deleted there): create it again.
      std::vector<readwise_api_utils::Highlight> one(1, h);
      if (!client.Create(one)) {
        failure = client.error();
        break;
      }
      created++;
    } else if ((work.update_note[i] && !client.UpdateNote(id, h.note)) ||
               !client.SetColorTag(id, h.color)) {
      failure = client.error();
      break;
    }
    h.readwise_uploaded = h.modified;
    h.readwise_id = id;
    done.push_back(h);
    if (id)
      updated++;
    if (done.size() >= 10) {
      library_files::SaveUploadState(app_, done);
      done.clear();
    }
  }
  library_files::SaveUploadState(app_, done);

  char line[96];
  snprintf(line, sizeof(line), "Sent %s, updated %s.",
           Plural(created, "new highlight").c_str(),
           Plural(updated, "highlight").c_str());
  app_.PrintStatus(std::string("READWISE ") + line +
                   (failure.empty() ? "" : " " + failure));
  if (failure.empty())
    SetLines(line, "Colors are added as tags. Deleting a",
             "highlight here doesn't delete it there.");
  else if (created + updated > 0)
    SetLines(line, failure);
  else
    SetLines("Upload failed:", failure);
}

void ReadwiseController::ExportCsv() {
  const library_files::HighlightSet set = library_files::CollectHighlights(app_);
  const readwise_export::Result result =
      readwise_export::ExportHighlights(set.highlights, set.books);
  if (result.ok)
    SetLines("Exported " + Plural(result.highlights, "highlight") + " from " +
                 Plural(result.books, "book") + " to",
             "exports/ on the SD card. Upload it",
             "at readwise.io/import_bulk.");
  else if (result.highlights == 0)
    SetLines("No highlights yet.");
  else
    SetLines("Export failed (is the SD card full?).");
}

void ReadwiseController::EnterToken() {
  std::string token;
  const bool entered = AskToken(&token);
  // The keyboard applet replaced both screens.
  app_.ts->MarkAllScreensDirty();
  dirty_ = true;
  if (!entered)
    return;
  pending_token_ = token;
  Start(kDoCheckToken, "Checking the token with Readwise...");
}

void ReadwiseController::CheckToken() {
  const std::string token = pending_token_;
  pending_token_.clear();
  https_client::Session session;
  https_client::Response response;
  const bool reached =
      session.ok() &&
      https_client::Request(session, "GET", kAuthUrl, AuthHeaders(token), "",
                            &response);
  if (reached && (response.status == 401 || response.status == 403)) {
    SetLines("Readwise didn't accept that token.", "Nothing was saved.");
    return;
  }
  if (!SaveToken(token)) {
    SetLines("Couldn't save the token (SD card?).");
    return;
  }
  if (reached && response.status == 204)
    SetLines("Token saved and accepted by Readwise.");
  else
    SetLines("Token saved, but Readwise couldn't be",
             "reached to check it now.");
}

void ReadwiseController::TestConnection() {
  const std::string token = LoadToken();
  https_client::Session session;
  if (!session.ok()) {
    SetLines("Couldn't start networking:", session.error());
    return;
  }
  const u64 start = osGetTime();
  https_client::Response response;
  const bool ok = https_client::Request(
      session, "GET", kAuthUrl,
      token.empty() ? std::vector<std::string>() : AuthHeaders(token), "",
      &response);
  char took[48];
  snprintf(took, sizeof(took), "(%.1f s)",
           (double)(osGetTime() - start) / 1000.0);
  if (!ok)
    SetLines("Couldn't reach Readwise:", response.error);
  else if (response.status == 204)
    SetLines("Connected; the token works.", took);
  else if (token.empty())
    SetLines("Connected to Readwise.", "No token set yet.", took);
  else if (response.status == 401 || response.status == 403)
    SetLines("Connected, but Readwise didn't", "accept the token.", took);
  else
    SetLines("Connected; Readwise answered HTTP " +
                 std::to_string((long long)response.status) + ".",
             took);
}

void ReadwiseController::RunFrame(const FrameInput &input) {
  if (pending_ != kNone) {
    // The "working" message was drawn last frame; do the work now.
    const Pending work = pending_;
    pending_ = kNone;
    if (work == kDoUpload)
      Upload();
    else if (work == kDoCheckToken)
      CheckToken();
    else if (work == kDoTest)
      TestConnection();
    if (dirty_)
      Draw();
    return;
  }

  const u32 keys = input.keys_down;
  touchPosition touch = {};
  const bool touched = (keys & KEY_TOUCH) != 0;
  if (touched)
    touch = app_.MapTouch(input);
  if ((keys & KEY_B) ||
      (touched && app_.buttonback.EnclosesPoint(touch.px, touch.py))) {
    Leave();
    return;
  }
  int chosen = -1;
  if (keys & (KEY_DUP | KEY_CPAD_UP)) {
    index_ = (index_ + kOptionCount - 1) % kOptionCount;
    dirty_ = true;
  } else if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) {
    index_ = (index_ + 1) % kOptionCount;
    dirty_ = true;
  } else if (keys & KEY_A) {
    chosen = index_;
  } else if (touched) {
    for (int i = 0; i < kOptionCount; i++) {
      Button button(app_.ts.get());
      LayoutButton(&button, i);
      if (button.EnclosesPoint(touch.px, touch.py))
        chosen = i;
    }
  }
  if (chosen >= 0)
    index_ = chosen;
  if (chosen == kUpload)
    Start(kDoUpload, "Uploading highlights to Readwise...");
  else if (chosen == kExportCsv)
    ExportCsv();
  else if (chosen == kToken)
    EnterToken();
  else if (chosen == kTest)
    Start(kDoTest, "Connecting to Readwise...");
  if (dirty_)
    Draw();
}

void ReadwiseController::Draw() {
  dirty_ = false;
  Text *ts = app_.ts.get();
  ts->SetScreen(ts->screenright);
  ts->ClearScreen();
  app_.DrawBottomGradientBackground();
  const int saved_style = ts->GetStyle();
  ts->SetStyle(TEXT_STYLE_BROWSER);
  const int row_h = ts->GetHeight() + 4;

  int y = 18;
  ts->SetPen(kTextX, y);
  ts->PrintString("Readwise");
  y += row_h + 4;
  for (size_t i = 0; i < lines_.size(); i++) {
    const std::vector<std::string> wrapped = Wrap(ts, lines_[i]);
    for (size_t w = 0; w < wrapped.size() && y < kButtonY0 - 4; w++) {
      ts->SetPen(kTextX, y);
      ts->PrintString(wrapped[w].c_str());
      y += row_h;
    }
  }

  const bool has_token = !LoadToken().empty();
  const char *labels[kOptionCount] = {"upload highlights", "export CSV file",
                                      "Readwise token", "test connection"};
  const char *hints[kOptionCount] = {
      "new and edited ones, on Wi-Fi >", "all books, to exports/ >",
      has_token ? "saved; A to change >" : "not set; A to type it >",
      "check Wi-Fi and token >"};
  for (int i = 0; i < kOptionCount; i++) {
    Button button(ts);
    LayoutButton(&button, i);
    button.SetLabel1(labels[i]);
    button.SetLabel2(hints[i]);
    button.Draw(ts->screenright, i == index_);
  }
  app_.buttonback.Draw(ts->screenright, false);
  ts->SetStyle(saved_style);
  ts->MarkScreenDirty(ts->screenright);
}
