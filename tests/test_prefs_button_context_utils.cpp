#include "settings/prefs_button_context_utils.h"

#include "test_assert.h"

int main() {
  settings::PrefsPageContext page;
  page.from_book = false;
  page.page = 0;
  page.fixed_layout = false;
  page.include_line_wrap_fix = false;
  test::ExpectEq("unified general page 1 count",
                 settings::PrefsPageButtonCount(page), 7);
  test::ExpectEq("unified general page 1 sync button",
                 settings::PrefsPageButtonForSlot(page, 6),
                 PREFS_BUTTON_SYNC_DEVICES);
  test::ExpectEq("unified general page 1 first button",
                 settings::PrefsPageButtonForSlot(page, 0),
                 PREFS_BUTTON_STYLE_CUSTOMIZATION);

  page.page = 1;
  test::ExpectEq("unified general style page count",
                 settings::PrefsPageButtonCount(page), 6);
  test::ExpectEq("unified general style page first button",
                 settings::PrefsPageButtonForSlot(page, 0),
                 PREFS_BUTTON_FONT_CONFIG);

  page.page = 2;
  test::ExpectEq("unified general options page count",
                 settings::PrefsPageButtonCount(page), 7);
  test::ExpectEq("unified general options page first button",
                 settings::PrefsPageButtonForSlot(page, 0),
                 PREFS_BUTTON_ORIENTATION);

  page.from_book = true;
  page.page = 0;
  page.include_line_wrap_fix = true;
  test::ExpectEq("unified MOBI page 1 count",
                 settings::PrefsPageButtonCount(page), 7);
  test::ExpectEq("unified MOBI line wrap slot",
                 settings::PrefsPageButtonForSlot(page, 1),
                 PREFS_BUTTON_LIBRARY_VIEW);

  page.page = 1;
  page.fixed_layout = false;
  test::ExpectEq("unified reflow style page count",
                 settings::PrefsPageButtonCount(page), 7);
  test::ExpectEq("unified reflow style page last button",
                 settings::PrefsPageButtonForSlot(page, 6),
                 PREFS_BUTTON_PUBLISHER_BLOCK_MARGINS);

  page.fixed_layout = true;
  test::ExpectEq("unified fixed-layout page 2 count",
                 settings::PrefsPageButtonCount(page), 3);
  test::ExpectEq("unified fixed-layout reading direction",
                 settings::PrefsPageButtonForSlot(page, 2),
                 PREFS_BUTTON_LIBRARY_VIEW);

  test::ExpectEq("general visible count",
                 settings::VisiblePrefsButtonCount(false, false), 7);
  test::ExpectEq("reflow book page 1 count",
                 settings::VisiblePrefsButtonCount(true, false), 6);
  test::ExpectEq("MOBI book page 1 count",
                 settings::VisiblePrefsButtonCount(true, true), 7);

  test::ExpectEq("general slot 0", settings::PrefsButtonForVisibleSlot(false, false, 0),
                 PREFS_BUTTON_STYLE_CUSTOMIZATION);
  test::ExpectEq("general slot 1", settings::PrefsButtonForVisibleSlot(false, false, 1),
                 PREFS_BUTTON_TIME24H);
  test::ExpectEq("general slot 2", settings::PrefsButtonForVisibleSlot(false, false, 2),
                 PREFS_BUTTON_TIME_REMAINING);
  test::ExpectEq("general slot 3", settings::PrefsButtonForVisibleSlot(false, false, 3),
                 PREFS_BUTTON_COLORMODE);
  test::ExpectEq("general slot 4", settings::PrefsButtonForVisibleSlot(false, false, 4),
                 PREFS_BUTTON_LIBRARY_VIEW);
  test::ExpectEq("general slot 5", settings::PrefsButtonForVisibleSlot(false, false, 5),
                 PREFS_BUTTON_LIBRARY_SORT);

  test::ExpectEq("general page 2 button count",
                 settings::ExtraPrefsButtonCount(), 7);
  test::ExpectEq("general page 2 orientation first",
                 settings::ExtraPrefsButtonForSlot(0),
                 PREFS_BUTTON_ORIENTATION);
  test::ExpectEq("general page 2 handedness second",
                 settings::ExtraPrefsButtonForSlot(1),
                 PREFS_BUTTON_HANDEDNESS);
  test::ExpectEq("general page 2 reopen last book",
                 settings::ExtraPrefsButtonForSlot(2),
                 PREFS_BUTTON_REOPEN_LAST_BOOK);
  test::ExpectEq("general page 2 circle pad",
                 settings::ExtraPrefsButtonForSlot(3),
                 PREFS_BUTTON_CIRCLE_PAD_PAGE_TURN);
  test::ExpectEq("general page 2 reset",
                 settings::ExtraPrefsButtonForSlot(4),
                 PREFS_BUTTON_RESET_DEFAULTS);
  test::ExpectEq("general page 2 clear cache",
                 settings::ExtraPrefsButtonForSlot(5),
                 PREFS_BUTTON_CLEAR_CACHE);
  test::ExpectEq("general page 2 export highlights",
                 settings::ExtraPrefsButtonForSlot(6),
                 PREFS_BUTTON_EXPORT_HIGHLIGHTS);

  test::ExpectEq("reflow page 1 slot 0",
                 settings::PrefsButtonForVisibleSlot(true, false, 0),
                 PREFS_BUTTON_STYLE_CUSTOMIZATION);
  test::ExpectEq("reflow page 1 slot 1",
                 settings::PrefsButtonForVisibleSlot(true, false, 1),
                 PREFS_BUTTON_TIME24H);
  test::ExpectEq("reflow page 1 slot 2",
                 settings::PrefsButtonForVisibleSlot(true, false, 2),
                 PREFS_BUTTON_TIME_REMAINING);
  test::ExpectEq("reflow page 1 slot 3",
                 settings::PrefsButtonForVisibleSlot(true, false, 3),
                 PREFS_BUTTON_BOOK_INFO);
  test::ExpectEq("reflow page 1 slot 4",
                 settings::PrefsButtonForVisibleSlot(true, false, 4),
                 PREFS_BUTTON_INDEX);
  test::ExpectEq("reflow page 1 slot 5",
                 settings::PrefsButtonForVisibleSlot(true, false, 5),
                 PREFS_BUTTON_BOOKMARKS);

  test::ExpectEq("MOBI page 1 slot 0",
                 settings::PrefsButtonForVisibleSlot(true, true, 0),
                 PREFS_BUTTON_STYLE_CUSTOMIZATION);
  test::ExpectEq("MOBI page 1 keeps line wrap fix",
                 settings::PrefsButtonForVisibleSlot(true, true, 1),
                 PREFS_BUTTON_LIBRARY_VIEW);
  test::ExpectEq("MOBI page 1 slot 2",
                 settings::PrefsButtonForVisibleSlot(true, true, 2),
                 PREFS_BUTTON_TIME24H);
  test::ExpectEq("MOBI page 1 slot 3",
                 settings::PrefsButtonForVisibleSlot(true, true, 3),
                 PREFS_BUTTON_TIME_REMAINING);
  test::ExpectEq("MOBI page 1 slot 4",
                 settings::PrefsButtonForVisibleSlot(true, true, 4),
                 PREFS_BUTTON_BOOK_INFO);
  test::ExpectEq("MOBI page 1 slot 5",
                 settings::PrefsButtonForVisibleSlot(true, true, 5),
                 PREFS_BUTTON_INDEX);
  test::ExpectEq("MOBI page 1 slot 6",
                 settings::PrefsButtonForVisibleSlot(true, true, 6),
                 PREFS_BUTTON_BOOKMARKS);

  test::ExpectEq("reflow page 2 count",
                 settings::BookPrefsPage2ButtonCount(false), 7);
  test::ExpectEq("reflow page 2 orientation first",
                 settings::BookPrefsPage2ButtonForSlot(false, 0),
                 PREFS_BUTTON_ORIENTATION);
  test::ExpectEq("reflow page 2 handedness second",
                 settings::BookPrefsPage2ButtonForSlot(false, 1),
                 PREFS_BUTTON_HANDEDNESS);
  test::ExpectEq("reflow page 2 font size",
                 settings::BookPrefsPage2ButtonForSlot(false, 2),
                 PREFS_BUTTON_FONTSIZE);
  test::ExpectEq("reflow page 2 publisher margins last",
                 settings::BookPrefsPage2ButtonForSlot(false, 6),
                 PREFS_BUTTON_PUBLISHER_BLOCK_MARGINS);

  test::ExpectEq("fixed-layout page 2 count",
                 settings::BookPrefsPage2ButtonCount(true), 3);
  test::ExpectEq("fixed-layout page 2 orientation first",
                 settings::BookPrefsPage2ButtonForSlot(true, 0),
                 PREFS_BUTTON_ORIENTATION);
  test::ExpectEq("fixed-layout page 2 handedness second",
                 settings::BookPrefsPage2ButtonForSlot(true, 1),
                 PREFS_BUTTON_HANDEDNESS);
  test::ExpectEq("fixed-layout page 2 reading direction third",
                 settings::BookPrefsPage2ButtonForSlot(true, 2),
                 PREFS_BUTTON_LIBRARY_VIEW);

  for (unsigned char slot = 0; slot < settings::VisiblePrefsButtonCount(true, false);
       slot++) {
    test::ExpectNe("book settings exclude Circle Pad setting",
                   settings::PrefsButtonForVisibleSlot(true, false, slot),
                   PREFS_BUTTON_CIRCLE_PAD_PAGE_TURN);
  }
  for (unsigned char slot = 0; slot < settings::VisiblePrefsButtonCount(true, true);
       slot++) {
    test::ExpectNe("book settings with line wrap exclude Circle Pad setting",
                   settings::PrefsButtonForVisibleSlot(true, true, slot),
                   PREFS_BUTTON_CIRCLE_PAD_PAGE_TURN);
  }

  return 0;
}
