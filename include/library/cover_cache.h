/*
    3dslibris - cover_cache.h
    New 3DS library module by Rigle.

    Summary:
    - Disk I/O for the browser cover thumbnail cache (.cvr files on SD).
    - Extracted from library/app_browser.cpp to isolate cache I/O from browser UI.
*/

#pragma once

#include <string>
#include <stdint.h>

class Book;

namespace cover_cache {

static const uint8_t kMaxAttempts = 3;

bool TryLoad(Book *book, const std::string &book_path);
// Stores book->coverPixels. A cover bigger than the grid thumbnail (what the
// extractors produce) is moved to book->largeCoverPixels and saved as the
// large cover, and coverPixels becomes the averaged-down thumbnail.
bool Save(Book *book, const std::string &book_path);
// Loads the large (top-screen) cover into book->largeCoverPixels.
bool TryLoadLarge(Book *book, const std::string &book_path);
// The large cover's file, from the thumbnail's (PathFor).
std::string LargePathFor(const std::string &thumb_path);
bool TryLoadAdjacentOverride(Book *book, const std::string &book_path);
// The thumbnail file for a book (call while the book file still exists:
// its size and date are part of the name).
std::string PathFor(Book *book, const std::string &book_path);

} // namespace cover_cache
