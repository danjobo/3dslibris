/*
    3dslibris - sync_manifest.h

    The library summary one console sends another: every book (matched by
    file name + size) with its per-book state (see annotation.h). Text
    format, built on the per-book state format:

      3DSLIBRIS-SYNC 1
      BOOK <file name> <size>
      <per-book state, annotation_store_utils v2 format>
      ENDBOOK
      ...

    Escaped text fields can't contain raw newlines, so ENDBOOK lines are
    unambiguous.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "book/annotation.h"

namespace sync_manifest {

struct BookEntry {
  std::string file_name;
  uint64_t file_size;
  BookState state;

  BookEntry() : file_size(0) {}
  std::string SyncId() const;
};

struct Manifest {
  std::vector<BookEntry> books;

  const BookEntry *Find(const std::string &sync_id) const;
};

std::string Serialize(const Manifest &manifest);
// Returns false for an unrecognized header; malformed books are skipped.
bool Parse(const std::string &data, Manifest *out);

} // namespace sync_manifest
