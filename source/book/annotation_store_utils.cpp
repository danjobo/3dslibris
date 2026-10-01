#include "book/annotation_store_utils.h"
#include "book/highlight_color_utils.h"

#include <stdio.h>
#include <stdlib.h>

namespace annotation_store_utils {

namespace {

static const char *kHeaderV1 = "3DSLIBRIS-ANNOTATIONS 1";
static const char *kHeaderV2 = "3DSLIBRIS-BOOKSTATE 2";
static const char *kHeaderV3 = "3DSLIBRIS-BOOKSTATE 3";
static const char *kHeaderV4 = "3DSLIBRIS-BOOKSTATE 4";
static const size_t kMaxFileBytes = 4 * 1024 * 1024;

void SplitTabs(const std::string &line, std::vector<std::string> *fields) {
  fields->clear();
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      fields->push_back(line.substr(start));
      return;
    }
    fields->push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

bool ParseU32(const std::string &s, unsigned long max, unsigned long *out) {
  if (s.empty())
    return false;
  char *end = NULL;
  const unsigned long v = strtoul(s.c_str(), &end, 10);
  if (!end || *end != '\0' || v > max)
    return false;
  *out = v;
  return true;
}

bool ParseHex64(const std::string &s, uint64_t *out) {
  if (s.empty() || s.size() > 16)
    return false;
  uint64_t v = 0;
  for (size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    int d;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    else
      return false;
    v = (v << 4) | (uint64_t)d;
  }
  *out = v;
  return true;
}

bool IsFatSafe(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

void AppendUnsigned(std::string *out, unsigned long v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu", v);
  *out += buf;
}

// v1: id created page_hint page_count_hint quote prefix note
bool ParseV1Line(const std::vector<std::string> &f, uint32_t console_prefix,
                 Annotation *a) {
  if (f.size() != 7)
    return false;
  unsigned long id = 0, created = 0, page = 0, count = 0;
  if (!ParseU32(f[0], 0xFFFFFFFFUL, &id) || id == 0 ||
      !ParseU32(f[1], 0xFFFFFFFFUL, &created) ||
      !ParseU32(f[2], 0xFFFFUL, &page) || !ParseU32(f[3], 0xFFFFUL, &count))
    return false;
  a->id = ((uint64_t)console_prefix << 32) | (uint64_t)id;
  a->kind = Annotation::kHighlight;
  a->created = (uint32_t)created;
  a->modified = (uint32_t)created;
  a->deleted = false;
  a->page_hint = (uint16_t)page;
  a->page_count_hint = (uint16_t)count;
  a->quote = UnescapeField(f[4]);
  a->prefix = UnescapeField(f[5]);
  a->note = UnescapeField(f[6]);
  return !a->quote.empty();
}

// v2 H/B: kind id created modified deleted page_hint page_count_hint quote
//         prefix note
// v2 records have 10 fields; v3 adds the highlight color, v4 the Readwise
// upload state.
bool ParseV2Record(const std::vector<std::string> &f, Annotation *a) {
  if ((f.size() != 10 && f.size() != 11 && f.size() != 13) ||
      (f[0] != "H" && f[0] != "B"))
    return false;
  unsigned long color = 0;
  if (f.size() >= 11 && !ParseU32(f[10], 0xFFUL, &color))
    return false;
  unsigned long uploaded = 0;
  a->readwise_uploaded = 0;
  a->readwise_id = 0;
  if (f.size() == 13) {
    if (!ParseU32(f[11], 0xFFFFFFFFUL, &uploaded) ||
        !ParseHex64(f[12], &a->readwise_id))
      return false;
    a->readwise_uploaded = (uint32_t)uploaded;
  }
  // Unknown colors (from a newer version) show as yellow.
  a->color = color < highlight_color_utils::kCount ? (uint8_t)color : 0;
  unsigned long created = 0, modified = 0, deleted = 0, page = 0, count = 0;
  if (!ParseHex64(f[1], &a->id) || a->id == 0 ||
      !ParseU32(f[2], 0xFFFFFFFFUL, &created) ||
      !ParseU32(f[3], 0xFFFFFFFFUL, &modified) ||
      !ParseU32(f[4], 1, &deleted) || !ParseU32(f[5], 0xFFFFUL, &page) ||
      !ParseU32(f[6], 0xFFFFUL, &count))
    return false;
  a->kind = f[0] == "H" ? Annotation::kHighlight : Annotation::kBookmark;
  a->created = (uint32_t)created;
  a->modified = (uint32_t)modified;
  a->deleted = deleted != 0;
  a->page_hint = (uint16_t)page;
  a->page_count_hint = (uint16_t)count;
  a->quote = UnescapeField(f[7]);
  a->prefix = UnescapeField(f[8]);
  a->note = UnescapeField(f[9]);
  // A live highlight needs text to anchor; bookmarks in fixed-layout books
  // and tombstones may have none.
  return !(a->kind == Annotation::kHighlight && !a->deleted &&
           a->quote.empty());
}

// v2 P: P last_read page_hint page_count_hint quote prefix
bool ParseV2Progress(const std::vector<std::string> &f, ReadingProgress *p) {
  if (f.size() != 6 || f[0] != "P")
    return false;
  unsigned long last_read = 0, page = 0, count = 0;
  if (!ParseU32(f[1], 0xFFFFFFFFUL, &last_read) ||
      !ParseU32(f[2], 0xFFFFUL, &page) || !ParseU32(f[3], 0xFFFFUL, &count))
    return false;
  p->last_read = (uint32_t)last_read;
  p->page_hint = (uint16_t)page;
  p->page_count_hint = (uint16_t)count;
  p->quote = UnescapeField(f[4]);
  p->prefix = UnescapeField(f[5]);
  return true;
}

} // namespace

std::string EscapeField(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    const char c = in[i];
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string UnescapeField(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    const char c = in[i];
    if (c != '\\' || i + 1 >= in.size()) {
      out.push_back(c);
      continue;
    }
    const char n = in[++i];
    switch (n) {
    case 't':
      out.push_back('\t');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    default:
      out.push_back(n);
      break;
    }
  }
  return out;
}

std::string Serialize(const BookState &state) {
  std::string out = kHeaderV4;
  out.push_back('\n');
  if (state.has_progress) {
    const ReadingProgress &p = state.progress;
    out += "P\t";
    AppendUnsigned(&out, p.last_read);
    out.push_back('\t');
    AppendUnsigned(&out, p.page_hint);
    out.push_back('\t');
    AppendUnsigned(&out, p.page_count_hint);
    out.push_back('\t');
    out += EscapeField(p.quote);
    out.push_back('\t');
    out += EscapeField(p.prefix);
    out.push_back('\n');
  }
  for (size_t i = 0; i < state.records.size(); i++) {
    const Annotation &a = state.records[i];
    char head[64];
    snprintf(head, sizeof(head), "%c\t%016llx\t", (char)a.kind,
             (unsigned long long)a.id);
    out += head;
    AppendUnsigned(&out, a.created);
    out.push_back('\t');
    AppendUnsigned(&out, a.modified);
    out.push_back('\t');
    out += a.deleted ? "1" : "0";
    out.push_back('\t');
    AppendUnsigned(&out, a.page_hint);
    out.push_back('\t');
    AppendUnsigned(&out, a.page_count_hint);
    out.push_back('\t');
    out += EscapeField(a.quote);
    out.push_back('\t');
    out += EscapeField(a.prefix);
    out.push_back('\t');
    out += EscapeField(a.note);
    out.push_back('\t');
    AppendUnsigned(&out, a.color);
    out.push_back('\t');
    AppendUnsigned(&out, a.readwise_uploaded);
    char rw_id[24];
    snprintf(rw_id, sizeof(rw_id), "\t%llx", (unsigned long long)a.readwise_id);
    out += rw_id;
    out.push_back('\n');
  }
  return out;
}

bool Parse(const std::string &data, uint32_t console_prefix, BookState *out) {
  if (!out)
    return false;
  *out = BookState();
  size_t pos = 0;
  int version = 0;
  std::vector<std::string> fields;
  while (pos <= data.size()) {
    size_t eol = data.find('\n', pos);
    if (eol == std::string::npos)
      eol = data.size();
    std::string line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line[line.size() - 1] == '\r')
      line.erase(line.size() - 1);

    if (version == 0) {
      if (line == kHeaderV1)
        version = 1;
      else if (line == kHeaderV2 || line == kHeaderV3 || line == kHeaderV4)
        version = 2; // v3/v4 records are v2 records plus trailing fields
      else
        return false;
      continue;
    }
    if (line.empty())
      continue;

    SplitTabs(line, &fields);
    if (version == 1) {
      Annotation a;
      if (ParseV1Line(fields, console_prefix, &a))
        out->records.push_back(a);
      continue;
    }
    if (!fields.empty() && fields[0] == "P") {
      ReadingProgress p;
      if (ParseV2Progress(fields, &p)) {
        out->progress = p;
        out->has_progress = true;
      }
      continue;
    }
    Annotation a;
    if (ParseV2Record(fields, &a))
      out->records.push_back(a);
  }
  return version != 0;
}

std::string BuildFileName(const std::string &folder,
                          const std::string &filename) {
  // FNV-1a over "folder/filename" keeps books with the same name in
  // different folders apart; the readable part is only for humans.
  const std::string key = folder + "/" + filename;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < key.size(); i++) {
    hash ^= (unsigned char)key[i];
    hash *= 16777619u;
  }
  std::string readable;
  for (size_t i = 0; i < filename.size() && readable.size() < 40; i++)
    readable.push_back(IsFatSafe(filename[i]) ? filename[i] : '_');
  char hex[16];
  snprintf(hex, sizeof(hex), "%08lx", (unsigned long)hash);
  return readable + "_" + hex + ".txt";
}

bool LoadFile(const std::string &path, uint32_t console_prefix,
              BookState *out) {
  if (!out)
    return false;
  *out = BookState();
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp) {
    // A crash between removing the old file and renaming the new one leaves
    // only the temporary copy behind.
    const std::string tmp = path + ".tmp";
    fp = fopen(tmp.c_str(), "rb");
    if (!fp)
      return true;
  }
  std::string data;
  char chunk[4096];
  size_t n = 0;
  while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
    data.append(chunk, n);
    if (data.size() > kMaxFileBytes)
      break;
  }
  fclose(fp);
  return Parse(data, console_prefix, out);
}

bool SaveFile(const std::string &path, const BookState &state) {
  if (state.Empty()) {
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    return true;
  }
  const std::string tmp = path + ".tmp";
  FILE *fp = fopen(tmp.c_str(), "wb");
  if (!fp)
    return false;
  const std::string data = Serialize(state);
  const bool wrote = fwrite(data.data(), 1, data.size(), fp) == data.size();
  const bool closed = fclose(fp) == 0;
  if (!wrote || !closed) {
    remove(tmp.c_str());
    return false;
  }
  // FAT rename does not replace an existing file.
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

uint64_t NextId(const BookState &state, uint32_t console_prefix) {
  uint32_t max_counter = 0;
  for (size_t i = 0; i < state.records.size(); i++) {
    const uint64_t id = state.records[i].id;
    if ((uint32_t)(id >> 32) != console_prefix)
      continue;
    const uint32_t counter = (uint32_t)(id & 0xFFFFFFFFu);
    if (counter > max_counter)
      max_counter = counter;
  }
  return ((uint64_t)console_prefix << 32) | (uint64_t)(max_counter + 1);
}

} // namespace annotation_store_utils
