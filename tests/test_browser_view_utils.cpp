#include "library/browser_view_utils.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string &message) {
  fprintf(stderr, "%s\n", message.c_str());
  std::exit(1);
}

void ExpectEq(const char *label, int actual, int expected) {
  if (actual != expected) {
    Fail(std::string(label) + ": expected " + std::to_string(expected) +
         ", got " + std::to_string(actual));
  }
}

void ExpectEq(const char *label, const char *actual, const char *expected) {
  if (std::string(actual ? actual : "") != std::string(expected ? expected : "")) {
    Fail(std::string(label) + ": expected " + expected + ", got " +
         (actual ? actual : "(null)"));
  }
}

void ExpectMode(const char *label, BrowserViewMode actual,
                BrowserViewMode expected) {
  if (actual != expected)
    Fail(std::string(label) + ": unexpected browser view mode");
}

void ExpectTrue(const char *label, bool value) {
  if (!value)
    Fail(std::string(label) + ": expected true");
}

void ExpectFalse(const char *label, bool value) {
  if (value)
    Fail(std::string(label) + ": expected false");
}

void ExpectNe(const char *label, unsigned actual, unsigned expected) {
  if (actual == expected) {
    Fail(std::string(label) + ": unexpected equal values");
  }
}

} // namespace

int main() {
  ExpectEq("gallery page size",
           browser_view_utils::PageSize(BROWSER_VIEW_GALLERY), 4);
  ExpectEq("gallery columns",
           browser_view_utils::ColumnCount(BROWSER_VIEW_GALLERY), 2);
  ExpectTrue("gallery loads covers",
             browser_view_utils::ShouldLoadCovers(BROWSER_VIEW_GALLERY));
  ExpectEq("gallery label", browser_view_utils::Label(BROWSER_VIEW_GALLERY),
           "Gallery");
  ExpectEq("gallery pref", browser_view_utils::ToPrefValue(BROWSER_VIEW_GALLERY),
           "gallery");

  ExpectEq("list page size", browser_view_utils::PageSize(BROWSER_VIEW_LIST), 7);
  ExpectEq("list columns", browser_view_utils::ColumnCount(BROWSER_VIEW_LIST), 1);
  ExpectEq("list title max lines", browser_view_utils::ListTitleMaxLines(), 2);
  ExpectEq("list title box height for 10px text",
           browser_view_utils::ListTitleBoxHeight(10), 32);
  ExpectFalse("list skips covers",
              browser_view_utils::ShouldLoadCovers(BROWSER_VIEW_LIST));
  ExpectEq("list label", browser_view_utils::Label(BROWSER_VIEW_LIST), "List");
  ExpectEq("list pref", browser_view_utils::ToPrefValue(BROWSER_VIEW_LIST),
           "list");

  ExpectMode("parse null defaults", browser_view_utils::ParsePrefValue(NULL),
             BROWSER_VIEW_GALLERY);
  ExpectMode("parse invalid defaults",
             browser_view_utils::ParsePrefValue("weird"),
             BROWSER_VIEW_GALLERY);
  ExpectMode("parse list", browser_view_utils::ParsePrefValue("list"),
             BROWSER_VIEW_LIST);

  ExpectEq("shelf page size", browser_view_utils::PageSize(BROWSER_VIEW_SHELF),
           9);
  ExpectEq("shelf columns", browser_view_utils::ColumnCount(BROWSER_VIEW_SHELF),
           3);
  ExpectTrue("shelf loads covers",
             browser_view_utils::ShouldLoadCovers(BROWSER_VIEW_SHELF));
  ExpectEq("shelf label", browser_view_utils::Label(BROWSER_VIEW_SHELF),
           "Shelf");
  ExpectMode("shelf pref round trip",
             browser_view_utils::ParsePrefValue(
                 browser_view_utils::ToPrefValue(BROWSER_VIEW_SHELF)),
             BROWSER_VIEW_SHELF);
  ExpectMode("toggle gallery -> shelf",
             browser_view_utils::NextMode(BROWSER_VIEW_GALLERY),
             BROWSER_VIEW_SHELF);
  ExpectMode("toggle shelf -> list",
             browser_view_utils::NextMode(BROWSER_VIEW_SHELF),
             BROWSER_VIEW_LIST);
  ExpectMode("toggle list -> gallery",
             browser_view_utils::NextMode(BROWSER_VIEW_LIST),
             BROWSER_VIEW_GALLERY);

  browser_view_utils::ListRowPalette selected =
      browser_view_utils::PaletteForListRow(true, 0);
  browser_view_utils::ListRowPalette normal =
      browser_view_utils::PaletteForListRow(false, 0);
  ExpectNe("selected text contrasts selected bg", (unsigned)selected.text,
           (unsigned)selected.fill);
  ExpectNe("normal text contrasts normal bg", (unsigned)normal.text,
           (unsigned)normal.fill);
  // Selection is indicated by fill color change, not text color.
  ExpectNe("selected and normal fill differ", (unsigned)selected.fill,
           (unsigned)normal.fill);

  return 0;
}
