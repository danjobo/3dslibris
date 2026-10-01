/*
    3dslibris - readwise_export.h

    Writes highlights and notes to a Readwise import CSV in
    paths::GetExportsDir().
*/

#pragma once

#include <string>
#include <vector>

class Book;

namespace readwise_export {

struct Result {
  bool ok;          // file written
  int highlights;   // rows exported
  int books;        // books that contributed rows
  std::string path; // written file

  Result() : ok(false), highlights(0), books(0) {}
};

// Exports every highlight of the given books (folders and fixed-layout
// books are skipped). Nothing is written when there are no highlights.
Result ExportBooks(const std::vector<Book *> &books);

} // namespace readwise_export
