#include "book/annotation_store_utils.h"

#include <stdio.h>
#include <stdlib.h>

namespace annotation_store_utils {

namespace {

static const char *kHeader = "3DSLIBRIS-ANNOTATIONS 1";
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

bool IsFatSafe(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
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

std::string Serialize(const std::vector<Annotation> &annotations) {
  std::string out = kHeader;
  out.push_back('\n');
  for (size_t i = 0; i < annotations.size(); i++) {
    const Annotation &a = annotations[i];
    char nums[80];
    snprintf(nums, sizeof(nums), "%lu\t%lu\t%u\t%u\t", (unsigned long)a.id,
             (unsigned long)a.created, (unsigned)a.page_hint,
             (unsigned)a.page_count_hint);
    out += nums;
    out += EscapeField(a.quote);
    out.push_back('\t');
    out += EscapeField(a.prefix);
    out.push_back('\t');
    out += EscapeField(a.note);
    out.push_back('\n');
  }
  return out;
}

bool Parse(const std::string &data, std::vector<Annotation> *out) {
  if (!out)
    return false;
  out->clear();
  size_t pos = 0;
  bool header_seen = false;
  std::vector<std::string> fields;
  while (pos <= data.size()) {
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
    if (line.empty())
      continue;

    SplitTabs(line, &fields);
    if (fields.size() != 7)
      continue;
    unsigned long id = 0, created = 0, page = 0, count = 0;
    if (!ParseU32(fields[0], 0xFFFFFFFFUL, &id) || id == 0 ||
        !ParseU32(fields[1], 0xFFFFFFFFUL, &created) ||
        !ParseU32(fields[2], 0xFFFFUL, &page) ||
        !ParseU32(fields[3], 0xFFFFUL, &count))
      continue;
    Annotation a;
    a.id = (uint32_t)id;
    a.created = (uint32_t)created;
    a.page_hint = (uint16_t)page;
    a.page_count_hint = (uint16_t)count;
    a.quote = UnescapeField(fields[4]);
    a.prefix = UnescapeField(fields[5]);
    a.note = UnescapeField(fields[6]);
    if (a.quote.empty())
      continue;
    out->push_back(a);
  }
  return header_seen;
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

bool LoadFile(const std::string &path, std::vector<Annotation> *out) {
  if (!out)
    return false;
  out->clear();
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
  return Parse(data, out);
}

bool SaveFile(const std::string &path,
              const std::vector<Annotation> &annotations) {
  if (annotations.empty()) {
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    return true;
  }
  const std::string tmp = path + ".tmp";
  FILE *fp = fopen(tmp.c_str(), "wb");
  if (!fp)
    return false;
  const std::string data = Serialize(annotations);
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

uint32_t NextId(const std::vector<Annotation> &annotations) {
  uint32_t max_id = 0;
  for (size_t i = 0; i < annotations.size(); i++)
    if (annotations[i].id > max_id)
      max_id = annotations[i].id;
  return max_id + 1;
}

} // namespace annotation_store_utils
