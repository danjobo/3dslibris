#include "sync/sync_manifest.h"

#include <stdio.h>
#include <stdlib.h>

#include "book/annotation_store_utils.h"
#include "sync/sync_merge.h"

namespace sync_manifest {

namespace {

static const char *kHeader = "3DSLIBRIS-SYNC 1";

bool ParseBookLine(const std::string &line, BookEntry *entry) {
  // BOOK<TAB>name<TAB>size
  const size_t a = line.find('\t');
  const size_t b = line.rfind('\t');
  if (a == std::string::npos || b == a || line.compare(0, a, "BOOK") != 0)
    return false;
  entry->file_name =
      annotation_store_utils::UnescapeField(line.substr(a + 1, b - a - 1));
  const std::string size = line.substr(b + 1);
  if (size.empty() || entry->file_name.empty())
    return false;
  char *end = NULL;
  const unsigned long long v = strtoull(size.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  entry->file_size = (uint64_t)v;
  return true;
}

} // namespace

std::string BookEntry::SyncId() const {
  return sync_merge::MakeSyncBookId(file_name, file_size);
}

const BookEntry *Manifest::Find(const std::string &sync_id) const {
  for (size_t i = 0; i < books.size(); i++)
    if (books[i].SyncId() == sync_id)
      return &books[i];
  return NULL;
}

std::string Serialize(const Manifest &manifest) {
  std::string out = kHeader;
  out.push_back('\n');
  for (size_t i = 0; i < manifest.books.size(); i++) {
    const BookEntry &book = manifest.books[i];
    char size[32];
    snprintf(size, sizeof(size), "%llu", (unsigned long long)book.file_size);
    out += "BOOK\t";
    out += annotation_store_utils::EscapeField(book.file_name);
    out.push_back('\t');
    out += size;
    out.push_back('\n');
    out += annotation_store_utils::Serialize(book.state);
    out += "ENDBOOK\n";
  }
  return out;
}

bool Parse(const std::string &data, Manifest *out) {
  if (!out)
    return false;
  out->books.clear();
  size_t pos = 0;
  bool header_seen = false;
  bool in_book = false;
  BookEntry current;
  std::string state_text;
  while (pos < data.size()) {
    size_t eol = data.find('\n', pos);
    if (eol == std::string::npos)
      eol = data.size();
    std::string line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line[line.size() - 1] == '\r')
      line.erase(line.size() - 1);

    if (!header_seen) {
      if (line != kHeader)
        return false;
      header_seen = true;
      continue;
    }
    if (!in_book) {
      if (line.compare(0, 5, "BOOK\t") == 0) {
        current = BookEntry();
        state_text.clear();
        in_book = ParseBookLine(line, &current);
      }
      continue;
    }
    if (line == "ENDBOOK") {
      // Records in a manifest already carry their final ids.
      if (annotation_store_utils::Parse(state_text, 0, &current.state))
        out->books.push_back(current);
      in_book = false;
      continue;
    }
    state_text += line;
    state_text.push_back('\n');
  }
  return header_seen;
}

} // namespace sync_manifest
