/*
    3dslibris - delete_book_controller.cpp

    See include/app/delete_book_controller.h.
*/

#include "app/delete_book_controller.h"

#include <3ds.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>

#include "app/app.h"
#include "app/frame_input.h"
#include "book/annotation_store_utils.h"
#include "book/book.h"
#include "library/cover_cache.h"
#include "settings/prefs.h"
#include "shared/path_constants.h"
#include "ui/button.h"
#include "ui/screen_layout_constants.h"
#include "ui/text.h"

namespace {

const int kButtonX = 5;
const int kButtonY0 = 150;
const int kButtonStride = 46;
const int kButtonW = 230;
const int kButtonH = 40;
const int kTextX = 8;
const int kTextWidth = 228;

void LayoutButton(Button *button, int index) {
  button->Init();
  button->SetStyle(BUTTON_STYLE_SETTING);
  button->Resize(kButtonW, kButtonH);
  button->Move(kButtonX, kButtonY0 + index * kButtonStride);
}

std::string FormatSize(unsigned long long bytes) {
  char buf[32];
  if (bytes >= 1024ULL * 1024ULL)
    snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
  else
    snprintf(buf, sizeof(buf), "%llu KB", (bytes + 1023ULL) / 1024ULL);
  return buf;
}

// Splits text into lines that fit the screen width (by whole characters,
// preferring spaces; widths are measured a few characters at a time
// because Text::GetStringWidth returns a u8).
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

} // namespace

DeleteBookController::DeleteBookController(App &app)
    : app_(app), book_(NULL), size_(0), built_in_(false), index_(kCancel),
      dirty_(true) {}

void DeleteBookController::Show(Book *book) {
  book_ = book;
  folder_ = book && book->GetFolderName() ? book->GetFolderName() : "";
  file_name_ = book && book->GetFileName() ? book->GetFileName() : "";
  title_ = book && book->GetTitle() ? book->GetTitle() : file_name_;
  built_in_ = folder_.compare(0, 6, "romfs:") == 0;
  size_ = 0;
  struct stat st;
  if (stat((folder_ + "/" + file_name_).c_str(), &st) == 0)
    size_ = (unsigned long long)st.st_size;
  // Start on "cancel" so a stray A press deletes nothing.
  index_ = kCancel;
  error_.clear();
  dirty_ = true;
  app_.buttonback.Move(screen_layout::kFooterMidX, screen_layout::kFooterY);
  app_.buttonback.Resize(screen_layout::kFooterMidW,
                         screen_layout::kFooterButtonH);
  app_.buttonback.Label("back");
  app_.buttonback.SetIcon(UI_BUTTON_ICON_BACK);
  app_.ts->SetScreen(app_.ts->screenright);
  app_.ts->MarkScreenDirty(app_.ts->screenright);
}

void DeleteBookController::Leave(int select_index) {
  book_ = NULL;
  app_.ShowLibraryAfterDelete(select_index);
}

void DeleteBookController::Delete(bool with_notes) {
  const std::string path = folder_ + "/" + file_name_;
  // Workers may be reading the file (covers, metadata), and the open book
  // holds it; stop both first.
  app_.PauseBrowserJobs();
  if (app_.GetCurrentBook() == book_)
    app_.CloseBook();
  // Its name depends on the file's size and date: work it out first.
  const std::string cover = cover_cache::PathFor(book_, path);
  if (remove(path.c_str()) != 0) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Couldn't delete the file (error %d).", errno);
    error_ = msg;
    dirty_ = true;
    return;
  }
  app_.PrintStatus("DELETE book " + path);
  if (!cover.empty()) {
    remove(cover.c_str());
    remove(cover_cache::LargePathFor(cover).c_str());
  }
  if (with_notes)
    remove((paths::GetAnnotationsDir() + "/" +
            annotation_store_utils::BuildFileName(folder_, file_name_))
               .c_str());
  const int index = app_.GetBookIndex(book_);
  // Rescanning drops the Book; then forget its saved page and bookmarks.
  Leave(index);
  if (app_.prefs) {
    app_.prefs->ForgetBook(folder_.c_str(), file_name_.c_str());
    app_.PersistPrefs();
  }
}

void DeleteBookController::RunFrame(const FrameInput &input) {
  const u32 keys = input.keys_down;
  touchPosition touch = {};
  const bool touched = (keys & KEY_TOUCH) != 0;
  if (touched)
    touch = app_.MapTouch(input);
  const bool back_touched =
      touched && app_.buttonback.EnclosesPoint(touch.px, touch.py);
  if (!book_ || (keys & KEY_B) || back_touched) {
    Leave(-1);
    return;
  }
  if (!error_.empty() || built_in_) {
    if (keys & KEY_A) {
      Leave(-1);
      return;
    }
    if (dirty_)
      Draw();
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
  if (chosen == kDeleteKeepNotes || chosen == kDeleteAll) {
    Delete(chosen == kDeleteAll);
    if (!book_)
      return; // deleted; back in the library
  } else if (chosen == kCancel) {
    Leave(-1);
    return;
  }
  if (dirty_)
    Draw();
}

void DeleteBookController::Draw() {
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
  ts->PrintString("delete book");
  y += row_h + 6;

  std::vector<std::string> lines = Wrap(ts, title_);
  if (lines.size() > 3)
    lines.resize(3);
  if (!error_.empty()) {
    lines.push_back("");
    std::vector<std::string> err = Wrap(ts, error_);
    lines.insert(lines.end(), err.begin(), err.end());
    lines.push_back("A: back");
  } else if (built_in_) {
    lines.push_back("");
    lines.push_back("This book is built into the app");
    lines.push_back("and can't be deleted.");
    lines.push_back("A: back");
  } else {
    lines.push_back(FormatSize(size_) +
                    (app_.GetCurrentBook() == book_ ? ", open (will close)"
                                                    : ""));
  }
  for (size_t i = 0; i < lines.size(); i++) {
    ts->SetPen(kTextX, y);
    ts->PrintString(lines[i].c_str());
    y += row_h;
  }

  if (error_.empty() && !built_in_) {
    static const char *kLabels[kOptionCount] = {
        "delete book", "delete book and its notes", "cancel"};
    static const char *kHints[kOptionCount] = {
        "keeps highlights & notes >", "removes highlights too >",
        "keep the book >"};
    for (int i = 0; i < kOptionCount; i++) {
      Button button(ts);
      LayoutButton(&button, i);
      button.SetLabel1(kLabels[i]);
      button.SetLabel2(kHints[i]);
      button.Draw(ts->screenright, i == index_);
    }
  }
  app_.buttonback.Draw(ts->screenright, false);
  ts->SetStyle(saved_style);
  ts->MarkScreenDirty(ts->screenright);
}
