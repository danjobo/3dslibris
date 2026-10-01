#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

struct SavedBookState {
  int position;
  bool mobi_line_wrap_fix;
  int style_font_size;
  int style_line_spacing;
  int style_paragraph_spacing;
  int style_publisher_text_indent;
  int style_publisher_block_margins;
  uint32_t last_opened;
  // For the library while the book is closed: its page count and reading
  // pace (ms per page) when it was last open. 0 = unknown.
  int page_count;
  uint32_t ms_per_page;
  std::vector<uint16_t> bookmarks;

  SavedBookState()
      : position(0), mobi_line_wrap_fix(false), style_font_size(-1),
        style_line_spacing(-1), style_paragraph_spacing(-1),
        style_publisher_text_indent(-1), style_publisher_block_margins(-1),
        last_opened(0), page_count(0), ms_per_page(0), bookmarks() {}
};

typedef std::unordered_map<std::string, SavedBookState> SavedBookStateMap;

std::string MakeSavedBookKey(const char *folder, const char *filename);
bool SplitSavedBookKey(const std::string &key, std::string *folder,
                       std::string *filename);
void RememberSavedBookState(SavedBookStateMap *states, const char *folder,
                            const char *filename,
                            const SavedBookState &state);
SavedBookState *FindSavedBookState(SavedBookStateMap *states,
                                  const char *folder, const char *filename);
const SavedBookState *FindSavedBookState(const SavedBookStateMap &states,
                                        const char *folder,
                                        const char *filename);
