/*
    3dslibris - annotation_store_utils.h

    Per-book highlight/note persistence. One file per book under
    paths::GetAnnotationsDir() (deliberately outside the cache directory,
    which "clear cache" deletes).

    Format: a header line, then one line per annotation with tab-separated
    fields (id, created, page_hint, page_count_hint, quote, prefix, note).
    Backslash, tab, CR and LF inside text fields are escaped as \\ \t \r \n.
*/

#pragma once

#include <string>
#include <vector>

#include "book/annotation.h"

namespace annotation_store_utils {

std::string EscapeField(const std::string &in);
std::string UnescapeField(const std::string &in);

std::string Serialize(const std::vector<Annotation> &annotations);
// Returns false for an unrecognized header. Malformed lines are skipped.
bool Parse(const std::string &data, std::vector<Annotation> *out);

// Stable, FAT-safe file name for a book, derived from its folder and name.
std::string BuildFileName(const std::string &folder,
                          const std::string &filename);

// Missing file loads as an empty list and returns true.
bool LoadFile(const std::string &path, std::vector<Annotation> *out);
// Writes path.tmp, then replaces path. Removes path when the list is empty.
bool SaveFile(const std::string &path,
              const std::vector<Annotation> &annotations);

uint32_t NextId(const std::vector<Annotation> &annotations);

} // namespace annotation_store_utils
