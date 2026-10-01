/*
    3dslibris - annotation_store_utils.h

    Per-book state file (highlights, bookmarks, reading position). One file
    per book under paths::GetAnnotationsDir() (deliberately outside the cache
    directory, which "clear cache" deletes).

    Format v4: a header line, then one record per line, tab-separated:
      H <id hex> <created> <modified> <deleted 0|1> <page_hint>
        <page_count_hint> <quote> <prefix> <note> <color>
        <readwise_uploaded> <readwise_id hex>               (highlight)
      B ...same fields, note empty, the rest 0...           (bookmark)
      P <last_read> <page_hint> <page_count_hint> <quote> <prefix>
    Backslash, tab, CR and LF inside text fields are escaped as \\ \t \r \n.

    Version 3 files (no Readwise fields) and version 2 files (no color
    either) still load: yellow, not uploaded.
    Version 1 files (highlights only, small per-book ids) still load; their
    ids are moved into the loading console's id space so they stay unique
    once synced.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "book/annotation.h"

namespace annotation_store_utils {

std::string EscapeField(const std::string &in);
std::string UnescapeField(const std::string &in);

std::string Serialize(const BookState &state);
// console_prefix: high 32 id bits given to records from a v1 file.
// Returns false for an unrecognized header. Malformed lines are skipped.
bool Parse(const std::string &data, uint32_t console_prefix, BookState *out);

// Stable, FAT-safe file name for a book, derived from its folder and name.
std::string BuildFileName(const std::string &folder,
                          const std::string &filename);

// Missing file loads as an empty state and returns true.
bool LoadFile(const std::string &path, uint32_t console_prefix,
              BookState *out);
// Writes path.tmp, then replaces path. Removes path when the state is empty.
bool SaveFile(const std::string &path, const BookState &state);

// Next id for a record created on this console.
uint64_t NextId(const BookState &state, uint32_t console_prefix);

} // namespace annotation_store_utils
