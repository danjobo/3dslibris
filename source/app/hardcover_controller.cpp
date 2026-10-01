/*
    3dslibris - hardcover_controller.cpp

    See include/app/hardcover_controller.h.
*/

#include "app/hardcover_controller.h"

#include <3ds.h>
#include <stdio.h>
#include <sys/stat.h>

#include "app/app.h"
#include "app/frame_input.h"
#include "app/https_client.h"
#include "book/book.h"
#include "shared/path_constants.h"
#include "sync/sync_merge.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "ui/text.h"

namespace {

const int kButtonX = 5;
const int kButtonY0 = 106;
const int kButtonStride = 46;
const int kButtonW = 230;
const int kButtonH = 40;
const int kVisibleRows = 4;
const int kTextX = 8;
const int kTextWidth = 228;
const size_t kMaxResults = 8;

std::string TokenPath() {
  return paths::GetSdmcBase() + "/hardcover-token.txt";
}
std::string LinksPath() {
  return paths::GetSdmcBase() + "/hardcover-links.txt";
}

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

void LayoutButton(Button *button, int row) {
  button->Init();
  button->SetStyle(BUTTON_STYLE_SETTING);
  button->Resize(kButtonW, kButtonH);
  button->Move(kButtonX, kButtonY0 + row * kButtonStride);
}

// Text::GetStringWidth returns a u8, so measure a character at a time.
std::vector<std::string> Wrap(Text *ts, const std::string &text,
                              size_t max_lines) {
  std::vector<std::string> lines;
  std::string rest = text;
  while (!rest.empty() && lines.size() < max_lines) {
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

// Fits a button label: whole characters, "..." when cut.
std::string Fit(Text *ts, const std::string &s, int max_px) {
  int width = 0;
  for (size_t i = 0; i < s.size();) {
    size_t next = i + 1;
    while (next < s.size() && ((unsigned char)s[next] & 0xC0) == 0x80)
      next++;
    width += ts->GetStringWidth(s.substr(i, next - i).c_str(),
                                TEXT_STYLE_BROWSER);
    if (width > max_px)
      return s.substr(0, i) + "...";
    i = next;
  }
  return s;
}

bool AskToken(std::string *out) {
  SwkbdState swkbd;
  static char buf[1024];
  buf[0] = '\0';
  swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 2, sizeof(buf) - 1);
  swkbdSetHintText(&swkbd, "Token from hardcover.app/account/api");
  swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Save", true);
  if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
    return false;
  *out = hardcover_utils::CleanToken(buf);
  return !out->empty();
}

} // namespace

HardcoverController::HardcoverController(App &app)
    : app_(app), book_(NULL), screen_(kMenu), work_(kWorkNone), on_close_(false),
      index_(0), results_top_(0), dirty_(true), left_(false) {}

std::string HardcoverController::LoadToken() const {
  std::string raw;
  ReadFile(TokenPath(), &raw);
  return hardcover_utils::CleanToken(raw);
}

std::vector<hardcover_utils::Link> HardcoverController::LoadLinks() const {
  std::string data;
  ReadFile(LinksPath(), &data);
  return hardcover_utils::ParseLinks(data);
}

bool HardcoverController::SaveLinks(
    const std::vector<hardcover_utils::Link> &links) const {
  return WriteFile(LinksPath(), hardcover_utils::SerializeLinks(links));
}

bool HardcoverController::LoadBookInfo(Book *book) {
  book_ = book;
  if (!book || !book->GetFileName() || !book->GetFolderName())
    return false;
  file_name_ = book->GetFileName();
  struct stat st;
  const std::string path =
      std::string(book->GetFolderName()) + "/" + file_name_;
  if (stat(path.c_str(), &st) != 0)
    return false;
  sync_id_ = sync_merge::MakeSyncBookId(file_name_, (uint64_t)st.st_size);
  title_ = book->GetTitle() && book->GetTitle()[0] ? book->GetTitle()
                                                   : file_name_;
  if (title_ == file_name_) {
    const size_t dot = title_.rfind('.');
    if (dot != std::string::npos && dot > 0)
      title_ = title_.substr(0, dot);
  }
  author_ = book->GetAuthor();
  return true;
}

int HardcoverController::CurrentProgressPage(const hardcover_utils::Link &link,
                                             bool *finished) const {
  const int position = book_->GetPosition();
  const int count = (int)book_->GetPageCount();
  *finished = hardcover_utils::IsFinished(position, count);
  return hardcover_utils::ProgressPage(position, count, link.pages);
}

void HardcoverController::SetLines(const std::string &a, const std::string &b,
                                   const std::string &c) {
  lines_.clear();
  lines_.push_back(a);
  if (!b.empty())
    lines_.push_back(b);
  if (!c.empty())
    lines_.push_back(c);
  dirty_ = true;
}

void HardcoverController::Show(Book *book) {
  on_close_ = false;
  work_ = kWorkNone;
  screen_ = kMenu;
  index_ = kFind;
  results_.clear();
  if (!LoadBookInfo(book)) {
    screen_ = kMessage;
    SetLines("No book file to link.");
  } else {
    const std::vector<hardcover_utils::Link> links = LoadLinks();
    const int i = hardcover_utils::FindLink(links, sync_id_);
    if (i >= 0)
      SetLines(title_, "Linked to: " + links[(size_t)i].title);
    else
      SetLines(title_, "Not linked to a Hardcover book yet.");
  }
  app_.buttonback.Move(screen_layout::kFooterMidX, screen_layout::kFooterY);
  app_.buttonback.Resize(screen_layout::kFooterMidW,
                         screen_layout::kFooterButtonH);
  app_.buttonback.Label("back");
  app_.buttonback.SetIcon(UI_BUTTON_ICON_BACK);
  app_.ts->SetScreen(app_.ts->screenright);
  app_.ts->MarkScreenDirty(app_.ts->screenright);
}

bool HardcoverController::WantsSendOnClose(Book *book) {
  if (!book || book->IsBrowserFolder() || book->GetPageCount() == 0 ||
      LoadToken().empty() || !LoadBookInfo(book))
    return false;
  const std::vector<hardcover_utils::Link> links = LoadLinks();
  const int i = hardcover_utils::FindLink(links, sync_id_);
  if (i < 0)
    return false;
  const hardcover_utils::Link &link = links[(size_t)i];
  bool finished = false;
  const int page = CurrentProgressPage(link, &finished);
  if (page <= 0 || (link.finished && finished))
    return false;
  if (page == link.last_sent_page && finished == link.finished)
    return false;
  std::map<std::string, int>::const_iterator tried = attempted_.find(sync_id_);
  return tried == attempted_.end() || tried->second != page;
}

void HardcoverController::ShowSendOnClose(Book *book) {
  Show(book);
  on_close_ = true;
  screen_ = kMessage;
  Start(kWorkSendOnClose, "Updating Hardcover...");
}

void HardcoverController::Start(Work work, const std::string &message) {
  work_ = work;
  SetLines(message, "This takes a few seconds...");
}

void HardcoverController::Finish() {
  const bool on_close = on_close_;
  on_close_ = false;
  left_ = true;
  if (on_close)
    app_.ShowLibraryView();
  else
    app_.ShowBookInfoView();
}

void HardcoverController::DoFind() {
  const std::string token = LoadToken();
  if (token.empty()) {
    SetLines("Set your Hardcover token first: put it",
             "in hardcover-token.txt in the 3dslibris",
             "folder, or choose \"Hardcover token\".");
    return;
  }
  https_client::Session session;
  if (!session.ok() || !session.HasNetwork()) {
    SetLines("Not connected to Wi-Fi.");
    return;
  }
  hardcover_client::Client client(session, token);
  const std::string isbn = hardcover_utils::IsbnFromFileName(file_name_);
  if (!client.FindCandidates(isbn, title_, author_, kMaxResults, &results_)) {
    SetLines("Search failed:", client.error());
    return;
  }
  if (results_.empty()) {
    SetLines("Nothing found on Hardcover for", title_);
    return;
  }
  screen_ = kResults;
  index_ = 0;
  results_top_ = 0;
  char line[64];
  snprintf(line, sizeof(line), "Pick the matching book (%d found):",
           (int)results_.size());
  SetLines(line, isbn.empty() ? std::string()
                              : "First: exact edition by ISBN " + isbn);
}

void HardcoverController::ChooseResult(int index) {
  const hardcover_client::Candidate &c = results_[(size_t)index];
  std::vector<hardcover_utils::Link> links = LoadLinks();
  int i = hardcover_utils::FindLink(links, sync_id_);
  if (i < 0) {
    links.push_back(hardcover_utils::Link());
    i = (int)links.size() - 1;
  }
  hardcover_utils::Link &link = links[(size_t)i];
  link.sync_id = sync_id_;
  link.book_id = c.book_id;
  link.edition_id = c.edition_id;
  link.pages = c.pages;
  link.title = c.title;
  link.last_sent_page = 0;
  link.finished = false;
  screen_ = kMenu;
  index_ = kSendNow;
  results_.clear();
  if (!SaveLinks(links)) {
    SetLines("Couldn't save the link (SD card?).");
    return;
  }
  if (c.pages <= 0)
    SetLines("Linked to: " + c.title,
             "Hardcover has no page count for it, so",
             "progress can't be sent. Try another edition.");
  else
    SetLines("Linked to: " + c.title,
             "Progress is sent when you leave the book.");
}

void HardcoverController::DoSend(bool on_close) {
  const std::string token = LoadToken();
  std::vector<hardcover_utils::Link> links = LoadLinks();
  const int i = hardcover_utils::FindLink(links, sync_id_);
  if (token.empty() || i < 0) {
    if (on_close) {
      Finish();
      return;
    }
    SetLines(token.empty() ? "Set your Hardcover token first."
                           : "Link the book to Hardcover first.");
    return;
  }
  hardcover_utils::Link &link = links[(size_t)i];
  bool finished = false;
  const int page = CurrentProgressPage(link, &finished);
  if (page <= 0) {
    if (on_close) {
      Finish();
      return;
    }
    SetLines("Hardcover has no page count for this", "book; link another edition.");
    return;
  }
  attempted_[sync_id_] = page;

  https_client::Session session;
  if (!session.ok() || !session.HasNetwork()) {
    // Leaving a book without Wi-Fi isn't worth a message.
    // (Still counts as attempted, so the library opening right after this
    // doesn't try again; the next page change will.)
    app_.PrintStatus("HARDCOVER skipped: no network");
    if (on_close) {
      Finish();
      return;
    }
    SetLines("Not connected to Wi-Fi.");
    return;
  }
  hardcover_client::Client client(session, token);
  char where[64];
  snprintf(where, sizeof(where), "page %d of %d%s", page, link.pages,
           finished ? ", marked Read" : "");
  if (!client.SendProgress(&link, page, finished)) {
    app_.PrintStatus("HARDCOVER failed: " + client.error());
    screen_ = kMessage;
    SetLines("Couldn't update Hardcover:", client.error(),
             on_close ? "A: continue" : "");
    return;
  }
  SaveLinks(links);
  app_.PrintStatus(std::string("HARDCOVER sent ") + where);
  if (on_close) {
    Finish();
    return;
  }
  SetLines("Hardcover updated:", where);
}

void HardcoverController::EnterToken() {
  std::string token;
  const bool entered = AskToken(&token);
  app_.ts->MarkAllScreensDirty();
  dirty_ = true;
  if (!entered)
    return;
  if (WriteFile(TokenPath(), token + "\n"))
    SetLines("Token saved. Use \"find on Hardcover\" to", "check it.");
  else
    SetLines("Couldn't save the token (SD card?).");
}

void HardcoverController::RunFrame(const FrameInput &input) {
  if (work_ != kWorkNone) {
    // Show the "working" message first; the (blocking) work runs once it
    // is on screen, on the next frame.
    if (dirty_) {
      Draw();
      return;
    }
    const Work work = work_;
    work_ = kWorkNone;
    left_ = false;
    if (work == kWorkFind)
      DoFind();
    else if (work == kWorkSend)
      DoSend(false);
    else if (work == kWorkSendOnClose)
      DoSend(true);
    if (!left_ && dirty_)
      Draw();
    return;
  }

  const u32 keys = input.keys_down;
  touchPosition touch = {};
  const bool touched = (keys & KEY_TOUCH) != 0;
  if (touched)
    touch = app_.MapTouch(input);
  const bool back = (keys & KEY_B) ||
                    (touched && app_.buttonback.EnclosesPoint(touch.px, touch.py));

  if (screen_ == kMessage) {
    if (back || (keys & KEY_A)) {
      if (on_close_ || !book_) {
        Finish();
        return;
      }
      screen_ = kMenu;
      dirty_ = true;
    }
  } else if (screen_ == kResults) {
    const int count = (int)results_.size();
    int chosen = -1;
    if (back) {
      screen_ = kMenu;
      results_.clear();
      SetLines(title_, "No book was linked.");
    } else if (keys & (KEY_DUP | KEY_CPAD_UP)) {
      index_ = (index_ + count - 1) % count;
      dirty_ = true;
    } else if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) {
      index_ = (index_ + 1) % count;
      dirty_ = true;
    } else if (keys & KEY_A) {
      chosen = index_;
    } else if (touched) {
      for (int r = 0; r < kVisibleRows && results_top_ + r < count; r++) {
        Button button(app_.ts.get());
        LayoutButton(&button, r);
        if (button.EnclosesPoint(touch.px, touch.py))
          chosen = results_top_ + r;
      }
    }
    if (index_ < results_top_)
      results_top_ = index_;
    if (index_ >= results_top_ + kVisibleRows)
      results_top_ = index_ - kVisibleRows + 1;
    if (chosen >= 0)
      ChooseResult(chosen);
  } else {
    if (back) {
      Finish();
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
      for (int r = 0; r < kOptionCount; r++) {
        Button button(app_.ts.get());
        LayoutButton(&button, r);
        if (button.EnclosesPoint(touch.px, touch.py))
          chosen = r;
      }
    }
    if (chosen >= 0)
      index_ = chosen;
    if (chosen == kFind) {
      Start(kWorkFind, "Searching Hardcover...");
    } else if (chosen == kSendNow) {
      Start(kWorkSend, "Sending progress to Hardcover...");
    } else if (chosen == kUnlink) {
      std::vector<hardcover_utils::Link> links = LoadLinks();
      const int i = hardcover_utils::FindLink(links, sync_id_);
      if (i >= 0) {
        links.erase(links.begin() + i);
        SaveLinks(links);
        SetLines(title_, "Unlinked. Hardcover keeps what was sent.");
      }
    } else if (chosen == kToken) {
      EnterToken();
    }
  }
  if (dirty_)
    Draw();
}

void HardcoverController::Draw() {
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
  ts->PrintString("Hardcover");
  y += row_h + 4;
  const int text_bottom = screen_ == kMessage ? screen_layout::kFooterY - 8
                                              : kButtonY0 - 4;
  for (size_t i = 0; i < lines_.size(); i++) {
    const std::vector<std::string> wrapped = Wrap(ts, lines_[i], 3);
    for (size_t w = 0; w < wrapped.size() && y < text_bottom; w++) {
      ts->SetPen(kTextX, y);
      ts->PrintString(wrapped[w].c_str());
      y += row_h;
    }
  }

  if (screen_ == kResults) {
    for (int r = 0; r < kVisibleRows; r++) {
      const int i = results_top_ + r;
      if (i >= (int)results_.size())
        break;
      const hardcover_client::Candidate &c = results_[(size_t)i];
      std::string line1 = c.title;
      if (c.year > 0)
        line1 += " (" + std::to_string((long long)c.year) + ")";
      std::string line2 = c.author.empty() ? std::string("unknown author")
                                           : c.author;
      line2 += c.pages > 0 ? ", " + std::to_string((long long)c.pages) + " p."
                           : ", no page count";
      if (c.edition_id)
        line2 += ", ISBN";
      Button button(ts);
      LayoutButton(&button, r);
      button.SetLabel1(Fit(ts, line1, kButtonW - 16).c_str());
      button.SetLabel2(Fit(ts, line2, kButtonW - 16).c_str());
      button.Draw(ts->screenright, i == index_);
    }
  } else if (screen_ == kMenu) {
    const std::vector<hardcover_utils::Link> links = LoadLinks();
    const int link_index = hardcover_utils::FindLink(links, sync_id_);
    std::string send_hint = "link the book first";
    if (link_index >= 0 && book_) {
      bool finished = false;
      const int page = CurrentProgressPage(links[(size_t)link_index], &finished);
      if (page > 0)
        send_hint = "page " + std::to_string((long long)page) + " of " +
                    std::to_string((long long)links[(size_t)link_index].pages) +
                    " >";
    }
    const bool has_token = !LoadToken().empty();
    const char *labels[kOptionCount] = {"find on Hardcover",
                                        "send progress now", "unlink",
                                        "Hardcover token"};
    const std::string hints[kOptionCount] = {
        "by ISBN, then title >", send_hint,
        link_index >= 0 ? "forget the link >" : "not linked",
        has_token ? "saved; A to change >" : "not set; A to type it >"};
    for (int r = 0; r < kOptionCount; r++) {
      Button button(ts);
      LayoutButton(&button, r);
      button.SetLabel1(labels[r]);
      button.SetLabel2(hints[r].c_str());
      button.Draw(ts->screenright, r == index_);
    }
  }
  app_.buttonback.Draw(ts->screenright, false);
  ts->SetStyle(saved_style);
  ts->MarkScreenDirty(ts->screenright);
}
