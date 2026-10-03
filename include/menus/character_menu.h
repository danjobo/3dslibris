/*
    3dslibris - character_menu.h

    The character list (AppMode::Characters): names saved from a selection
    (hold X > Add as character), each with the first sentence it appears
    in, and for one character every mention up to where you are reading,
    with the sentence around it. A on a mention jumps there with the name
    underlined; pages turn as usual, and B comes back to the list and to
    the page you were reading. Mentions past that page are never shown,
    so the list can't spoil what's ahead.
*/

#pragma once

#include <3ds.h>
#include <string>
#include <vector>

#include "book/annotation.h"
#include "book/character_utils.h"
#include "menus/paged_list_menu.h"

class CharacterMenu : public PagedListMenu {
public:
  explicit CharacterMenu(class App *app);

  // Starts showing the list for the current book, or one character's
  // mentions (character_id != 0). The search runs in Prepare().
  void Open(uint64_t character_id);
  bool NeedsPrepare() const { return !prepared_; }
  // Finds the mentions (blocking; ~1 s for a long book on Old 3DS).
  void Prepare();

  // B on a character's mentions goes back to the list; X removes a
  // character (press twice).
  void HandleInput(const FrameInput &input) override;

  // Reading a page reached from a mention of this book.
  bool InJump(const class Book *book) const {
    return jump_active_ && book && book == origin_book_;
  }
  // Ends the jump: back to the page you were reading, no underlines.
  void EndJump();
  // Shows the list again where it was before the jump (no new search).
  void Resume();

protected:
  void BuildEntries(class Book *book, class Text *text,
                    std::vector<std::string> &labels,
                    std::vector<u16> &pages) override;
  bool ResolveTargetPage(u16 index, u16 *page_out) override;

private:
  enum Level { kList, kMentions };
  enum RowKind { kReturnRow, kCharacterRow, kMentionRow, kEmptyRow };
  struct Row {
    RowKind kind;
    int value; // character index, or mention page
  };

  void ShowList();
  std::string Snippet(class Text *text, const std::string &s) const;

  Level level_;
  bool prepared_;
  bool opened_on_mentions_; // B leaves instead of going to the list
  uint64_t open_id_;
  std::vector<Annotation> characters_;
  std::vector<std::vector<character_utils::Mention> > mentions_;
  int character_; // shown in kMentions
  std::vector<Row> rows_;
  // Where the reader was when the list was first opened: the spoiler
  // limit, and where "Back to page N" goes.
  class Book *origin_book_;
  int origin_page_;
  uint64_t remove_armed_id_;
  bool jump_active_;
  int jump_mention_; // index of the mention jumped to, in its list
};
