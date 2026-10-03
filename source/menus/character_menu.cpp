/*
    3dslibris - character_menu.cpp

    See include/menus/character_menu.h.
*/

#include "menus/character_menu.h"

#include <algorithm>
#include <stdio.h>

#include "book/book.h"
#include "dictionary/word_lookup_utils.h"
#include "shared/text_token_constants.h"
#include "ui/text.h"

namespace {

const size_t kMaxMentionsPerName = 500;
const int kSnippetWidthPx = 218;
const int kSnippetLines = 2;

int BrowserAdvance(void *ctx, uint32_t codepoint) {
  return (int)static_cast<Text *>(ctx)->GetAdvance(codepoint);
}

std::string PageLabel(int page) {
  char buf[32];
  snprintf(buf, sizeof(buf), "p. %d", page + 1);
  return buf;
}

} // namespace

CharacterMenu::CharacterMenu(App *app)
    : PagedListMenu(app, "mentions"), level_(kList), prepared_(true),
      opened_on_mentions_(false), open_id_(0), character_(-1),
      origin_book_(NULL), origin_page_(0), remove_armed_id_(0),
      jump_active_(false), jump_mention_(0) {}

void CharacterMenu::EndJump() {
  if (!jump_active_)
    return;
  jump_active_ = false;
  if (origin_book_) {
    origin_book_->SetMarkedName(std::string());
    origin_book_->SetPosition((u16)origin_page_);
  }
}

void CharacterMenu::Resume() {
  Init();
  // The mention we jumped to, below the "Back to page" row if there is one.
  for (size_t i = 0; i < rows_.size(); i++) {
    if (rows_[i].kind == kMentionRow) {
      SelectItem((u16)(i + (size_t)jump_mention_));
      break;
    }
  }
}

void CharacterMenu::Open(uint64_t character_id) {
  EndJump();
  open_id_ = character_id;
  opened_on_mentions_ = character_id != 0;
  level_ = kList;
  character_ = -1;
  remove_armed_id_ = 0;
  prepared_ = false;
}

void CharacterMenu::Prepare() {
  prepared_ = true;
  characters_.clear();
  mentions_.clear();
  Book *book = ContextBook();
  if (!book)
    return;
  // Reading on past the last origin starts a new one.
  const int current = (int)book->GetPosition();
  if (origin_book_ != book || current > origin_page_) {
    origin_book_ = book;
    origin_page_ = current;
  }
  characters_ = book->GetCharacters();
  std::vector<std::string> names;
  for (size_t i = 0; i < characters_.size(); i++)
    names.push_back(characters_[i].quote);
  const int last_page =
      std::min(origin_page_, (int)book->GetPageCount() - 1);
  character_utils::FindMentions(names, &Book::PageBufferForSearch, book,
                                last_page, kMaxMentionsPerName, &mentions_);
  for (size_t i = 0; i < characters_.size(); i++) {
    if (open_id_ && characters_[i].id == open_id_) {
      level_ = kMentions;
      character_ = (int)i;
    }
  }
  if (level_ != kMentions)
    opened_on_mentions_ = false;
}

std::string CharacterMenu::Snippet(Text *text, const std::string &s) const {
  if (!text || s.empty())
    return std::string();
  const int saved_style = text->GetStyle();
  text->SetStyle(TEXT_STYLE_BROWSER);
  const std::vector<word_lookup_utils::WrappedLine> wrapped =
      word_lookup_utils::WrapText(s, kSnippetWidthPx, &BrowserAdvance, text);
  text->SetStyle(saved_style);
  std::string out;
  for (size_t i = 0; i < wrapped.size() && (int)i < kSnippetLines; i++) {
    std::string line = wrapped[i].text;
    if ((int)i == kSnippetLines - 1 && wrapped.size() > (size_t)kSnippetLines)
      line += "...";
    out += "\n" + line;
  }
  return out;
}

void CharacterMenu::BuildEntries(Book *book, Text *text,
                                 std::vector<std::string> &labels,
                                 std::vector<u16> &pages) {
  rows_.clear();
  if (!book)
    return;
  const int current = (int)book->GetPosition();
  if (origin_book_ == book && current != origin_page_) {
    labels.push_back("Back to page " + std::to_string((long long)origin_page_ + 1) +
                     "\n(where you were)");
    pages.push_back((u16)origin_page_);
    Row row = {kReturnRow, origin_page_};
    rows_.push_back(row);
  }

  if (level_ == kMentions && character_ >= 0 &&
      character_ < (int)characters_.size()) {
    const std::vector<character_utils::Mention> &list =
        mentions_[(size_t)character_];
    char title[96];
    snprintf(title, sizeof(title), "%.60s: %d mention%s so far",
             characters_[(size_t)character_].quote.c_str(), (int)list.size(),
             list.size() == 1 ? "" : "s");
    SetHeaderTitle(title);
    for (size_t i = 0; i < list.size(); i++) {
      labels.push_back(PageLabel(list[i].page) + Snippet(text, list[i].snippet));
      pages.push_back((u16)list[i].page);
      Row row = {kMentionRow, list[i].page};
      rows_.push_back(row);
    }
    if (list.empty()) {
      labels.push_back("Not mentioned yet\n(only pages up to where you are\nreading are searched)");
      pages.push_back((u16)current);
      Row row = {kEmptyRow, 0};
      rows_.push_back(row);
    }
    return;
  }

  SetHeaderTitle(remove_armed_id_ ? "mentions  X again: remove"
                                  : "mentions  X:remove");
  for (size_t i = 0; i < characters_.size(); i++) {
    const std::vector<character_utils::Mention> &list = mentions_[i];
    std::string label = characters_[i].quote + "  (" +
                        std::to_string((long long)list.size()) + ")";
    label += Snippet(text, list.empty() ? std::string("Not mentioned yet.")
                                        : list[0].snippet);
    labels.push_back(label);
    pages.push_back(list.empty() ? (u16)current : (u16)list[0].page);
    Row row = {kCharacterRow, (int)i};
    rows_.push_back(row);
  }
  if (characters_.empty()) {
    labels.push_back("Nothing tracked yet\nHold X on a name or word, select\nit, and choose \"Track mentions\".");
    pages.push_back((u16)current);
    Row row = {kEmptyRow, 0};
    rows_.push_back(row);
  }
}

bool CharacterMenu::ResolveTargetPage(u16 index, u16 *page_out) {
  if (index >= rows_.size())
    return false;
  const Row row = rows_[index];
  switch (row.kind) {
  case kCharacterRow:
    level_ = kMentions;
    character_ = row.value;
    remove_armed_id_ = 0;
    Init();
    return false;
  case kReturnRow:
    // Back where we were: the next opening starts from here.
    origin_book_ = NULL;
    *page_out = (u16)row.value;
    return true;
  case kMentionRow: {
    Book *book = ContextBook();
    if (book && character_ >= 0 && character_ < (int)characters_.size()) {
      book->SetMarkedName(characters_[(size_t)character_].quote);
      jump_active_ = true;
      jump_mention_ = 0;
      for (size_t i = 0; i < index && i < rows_.size(); i++)
        if (rows_[i].kind == kMentionRow)
          jump_mention_++;
    }
    *page_out = (u16)row.value;
    return true;
  }
  default:
    return false;
  }
}

void CharacterMenu::ShowList() {
  level_ = kList;
  character_ = -1;
  remove_armed_id_ = 0;
  Init();
}

void CharacterMenu::HandleInput(const FrameInput &input) {
  const u32 keys = input.keys_down;
  if (level_ == kMentions && !opened_on_mentions_ && (keys & KEY_B)) {
    ShowList();
    return;
  }
  if (level_ == kList && (keys & KEY_X)) {
    Book *book = ContextBook();
    if (book && selected < rows_.size() &&
        rows_[selected].kind == kCharacterRow) {
      const size_t i = (size_t)rows_[selected].value;
      const uint64_t id = characters_[i].id;
      if (remove_armed_id_ == id) {
        book->RemoveCharacter(id);
        characters_.erase(characters_.begin() + (long)i);
        mentions_.erase(mentions_.begin() + (long)i);
        remove_armed_id_ = 0;
        Init();
      } else {
        remove_armed_id_ = id;
        SetHeaderTitle("mentions  X again: remove");
        SetDirty();
      }
    }
    return;
  }
  if (remove_armed_id_ && (keys & ~KEY_X)) {
    remove_armed_id_ = 0;
    SetHeaderTitle("mentions  X:remove");
    SetDirty();
  }
  PagedListMenu::HandleInput(input);
}
