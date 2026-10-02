/*
    3dslibris - word_lookup_panel.h

    The scrolling text panel that shows a looked-up word's definitions on
    the touch screen, over the page (word lookup mode, hold Y). It is
    painted into the touch screen's buffer like the selection popup; the
    page underneath is restored from the selection snapshot when it closes.
*/

#pragma once

#include <string>
#include <vector>

class Text;

namespace word_lookup_panel {

struct Section {
  std::string heading; // e.g. "WordNet", "Wikipedia: Baghdad"
  std::string text;
};

// Wraps the sections for the touch screen and shows them from the top.
// footer: key hints for the bottom line. bottom_reserved_px: space kept
// clear at the bottom (the landscape status bar is drawn there).
void Show(Text *ts, const std::string &title,
          const std::vector<Section> &sections, const std::string &footer,
          int bottom_reserved_px);
void Hide();
bool IsVisible();

// Scrolls by lines (negative: up); pages = true scrolls by a screenful.
// False if it was already at that end.
bool Scroll(Text *ts, int amount, bool pages);

// Paints the panel into ts->screenright.
void Draw(Text *ts);

} // namespace word_lookup_panel
