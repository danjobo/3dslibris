/*
    3dslibris - library_details_view.h

    The library's top screen while a book is selected: its cover, large,
    then title, author, a progress bar, the page and the time left.
*/

#pragma once

class Book;
class Text;

namespace library_details_view {

// Draws onto ts->screenleft, over a background the caller has drawn.
void Draw(Text *ts, Book *book);

} // namespace library_details_view
