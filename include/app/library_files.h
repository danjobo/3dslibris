/*
    3dslibris - library_files.h

    Every book in the library, in all folders (not just the folder the
    library view shows), and the highlights in them. Used by sync and by
    the Readwise export and upload. Device only.
*/

#pragma once

#include <string>
#include <vector>

#include "book/readwise_api_utils.h"
#include "sync/sync_book_files.h"

class App;
class Book;

namespace library_files {

// True for files the library lists as books.
bool IsLibraryBook(const char *file_name);
// The SD book folder (all subfolders), then the books built into the app.
std::vector<sync_book_files::LocalBook> ScanAll(const App &app);

struct HighlightSet {
  std::vector<readwise_api_utils::Highlight> highlights;
  int books; // books with at least one highlight
  HighlightSet() : books(0) {}
};
// Every live highlight in the library, with its book's title and author.
HighlightSet CollectHighlights(App &app);
// The same for one loaded book (e.g. the one being closed).
HighlightSet CollectBookHighlights(Book *book);
// Stores each highlight's readwise_uploaded / readwise_id in its book's
// state (the open or listed Book, or the state file). Returns how many.
int SaveUploadState(App &app,
                    const std::vector<readwise_api_utils::Highlight> &done);

} // namespace library_files
