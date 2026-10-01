/*
    3dslibris - prefs.h
    Adapted from dslibris for Nintendo 3DS.

    Original attribution (dslibris): Ray Haleblian, GPLv2+.
    Modified for Nintendo 3DS by Rigle.

    Summary:
    - Preferences persistence API (read/apply/write).
    - Stores input/display options and per-book state in XML prefs file.
*/

#pragma once

#include "library/browser_view_mode.h"
#include "library/library_sort_mode.h"
#include "settings/prefs_book_state.h"

#include <stdint.h>
#include <string>
#include <vector>

class App;
class Book;

class Prefs {
public:
  Prefs(App *app);
  ~Prefs();
  void Apply();
  int Read();
  int Write();
  void ClearPendingCurrentBookRestore();
  void SetPendingCurrentBookRestore(const char *folder, const char *filename,
                                    int position,
                                    bool mobi_line_wrap_fix,
                                    int style_font_size,
                                    int style_line_spacing,
                                    int style_paragraph_spacing,
                                    int style_publisher_text_indent,
                                    int style_publisher_block_margins);
  void AddPendingCurrentBookBookmark(uint16_t page);
  void EndPendingCurrentBookRestoreEntry();
  bool ApplyPendingCurrentBookRestore();
  void RememberSavedLastOpened(const char *folder, const char *filename,
                               uint32_t last_opened);
  void RememberSavedBookState(const char *folder, const char *filename,
                              int position, bool mobi_line_wrap_fix,
                              int style_font_size, int style_line_spacing,
                              int style_paragraph_spacing,
                              int style_publisher_text_indent,
                              int style_publisher_block_margins,
                              uint32_t last_opened);
  void BeginSavedBookBookmarks(const char *folder, const char *filename);
  void RememberSavedBookBookmark(uint16_t page);
  void EndSavedBookBookmarks();
  void ApplySavedBookState(Book *book) const;
  // Sync: sets the bookmarks of a book that isn't loaded and moves it to a
  // 0-based page (page < 0 keeps its position).
  void ApplySyncedBookPages(const char *folder, const char *filename, int page,
                            const std::vector<uint16_t> &bookmarks);
  App *GetApp() const { return app; }
  long modtime;
  bool swapshoulder;
  bool time24h;
  bool show_time_remaining;
  // Color for new highlights: the last one chosen (highlight_color_utils).
  uint8_t highlight_color;
  BrowserViewMode browser_view_mode;
  bool fixed_layout_rtl;
  bool circle_pad_page_turn;
  LibrarySortMode library_sort_mode;

private:
  App *app;
  bool pending_current_book_restore;
  bool collecting_pending_current_book;
  std::string pending_current_folder;
  std::string pending_current_filename;
  int pending_current_position;
  bool pending_current_mobi_line_wrap_fix;
  int pending_current_style_font_size;
  int pending_current_style_line_spacing;
  int pending_current_style_paragraph_spacing;
  int pending_current_style_publisher_text_indent;
  int pending_current_style_publisher_block_margins;
  std::vector<uint16_t> pending_current_bookmarks;
  std::unordered_map<std::string, uint32_t> last_opened_by_book_key;
  SavedBookStateMap saved_state_by_book_key;
  std::string saved_bookmarks_key;
  void Init();
};
