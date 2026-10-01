/*
    3dslibris - readwise_api_utils.cpp

    See include/book/readwise_api_utils.h.
*/

#include "book/readwise_api_utils.h"

#include <stdio.h>
#include <stdlib.h>

#include "book/highlight_color_utils.h"

#include <ctype.h>

namespace readwise_api_utils {

namespace {

const char kLogHeader[] = "3DSLIBRIS-READWISE 1";

// Cuts at max_chars codepoints without splitting a UTF-8 sequence.
std::string Truncate(const std::string &s, size_t max_chars) {
  size_t chars = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (((unsigned char)s[i] & 0xC0) == 0x80)
      continue;
    if (chars == max_chars)
      return s.substr(0, i);
    chars++;
  }
  return s;
}

void AppendField(std::string *out, const char *name, const std::string &value,
                 bool *first) {
  if (!*first)
    out->push_back(',');
  *first = false;
  out->push_back('"');
  *out += name;
  *out += "\":";
  *out += value;
}

// Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm).
void CivilFromDays(long long z, int *y, unsigned *m, unsigned *d) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long year = (long long)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = (int)(year + (*m <= 2 ? 1 : 0));
}

} // namespace

std::string JsonString(const std::string &utf8) {
  std::string out = "\"";
  for (size_t i = 0; i < utf8.size(); i++) {
    const unsigned char c = (unsigned char)utf8[i];
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out.push_back((char)c); // UTF-8 passes through
      }
      break;
    }
  }
  out.push_back('"');
  return out;
}

std::string IsoTime(uint32_t unix_time) {
  const long long days = (long long)(unix_time / 86400u);
  const uint32_t secs = unix_time % 86400u;
  int y = 0;
  unsigned m = 0, d = 0;
  CivilFromDays(days, &y, &m, &d);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02u:%02u:%02u+00:00", y, m, d,
           secs / 3600u, (secs / 60u) % 60u, secs % 60u);
  return buf;
}

std::string BuildHighlightsJson(const std::vector<Highlight> &highlights) {
  std::string out = "{\"highlights\":[";
  for (size_t i = 0; i < highlights.size(); i++) {
    const Highlight &h = highlights[i];
    if (i > 0)
      out.push_back(',');
    out.push_back('{');
    bool first = true;
    AppendField(&out, "text", JsonString(Truncate(h.text, kMaxTextChars)),
                &first);
    AppendField(&out, "title", JsonString(Truncate(h.title, kMaxTitleChars)),
                &first);
    if (!h.author.empty())
      AppendField(&out, "author",
                  JsonString(Truncate(h.author, kMaxAuthorChars)), &first);
    AppendField(&out, "note",
                JsonString(Truncate(NoteWithTag(h.color, h.note),
                                    kMaxNoteChars)),
                &first);
    AppendField(&out, "category", "\"books\"", &first);
    AppendField(&out, "source_type", "\"3dslibris\"", &first);
    if (h.location > 0) {
      char num[16];
      snprintf(num, sizeof(num), "%d", h.location);
      AppendField(&out, "location", num, &first);
      AppendField(&out, "location_type", "\"page\"", &first);
    }
    if (h.highlighted_at)
      AppendField(&out, "highlighted_at", JsonString(IsoTime(h.highlighted_at)),
                  &first);
    out.push_back('}');
  }
  out += "]}";
  return out;
}

std::string SerializeLog(const UploadLog &log) {
  std::string out = kLogHeader;
  out.push_back('\n');
  for (UploadLog::const_iterator it = log.begin(); it != log.end(); ++it) {
    char line[48];
    snprintf(line, sizeof(line), "%016llx\t%lu\n",
             (unsigned long long)it->first, (unsigned long)it->second);
    out += line;
  }
  return out;
}

UploadLog ParseLog(const std::string &data) {
  UploadLog log;
  size_t pos = data.find('\n');
  if (pos == std::string::npos || data.compare(0, pos, kLogHeader) != 0)
    return log;
  pos++;
  while (pos < data.size()) {
    size_t eol = data.find('\n', pos);
    if (eol == std::string::npos)
      eol = data.size();
    const std::string line = data.substr(pos, eol - pos);
    pos = eol + 1;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0)
      continue;
    char *end = NULL;
    const unsigned long long id =
        strtoull(line.substr(0, tab).c_str(), &end, 16);
    if (!end || *end != '\0' || id == 0)
      continue;
    const unsigned long modified =
        strtoul(line.substr(tab + 1).c_str(), &end, 10);
    if (!end || (*end != '\0' && *end != '\r'))
      continue;
    log[(uint64_t)id] = (uint32_t)modified;
  }
  return log;
}

std::string ColorTag(uint8_t color) {
  return highlight_color_utils::Name(color);
}

std::string NoteWithTag(uint8_t color, const std::string &note) {
  const std::string tag = "." + ColorTag(color);
  return note.empty() ? tag : tag + "\n" + note;
}

Work Classify(const std::vector<Highlight> &all, const UploadLog &legacy_log) {
  Work work;
  for (size_t i = 0; i < all.size(); i++) {
    const Highlight &h = all[i];
    if (h.readwise_uploaded != 0) {
      if (h.readwise_uploaded == h.modified)
        continue; // up to date
      work.update.push_back(h);
      work.update_note.push_back(true);
      continue;
    }
    UploadLog::const_iterator it = legacy_log.find(h.id);
    if (it == legacy_log.end()) {
      work.create.push_back(h);
    } else {
      // Sent by an older version: no color tag; the note too if edited.
      work.update.push_back(h);
      work.update_note.push_back(it->second != h.modified);
    }
  }
  return work;
}

bool SameText(const std::string &a, const std::string &b) {
  size_t ab = 0, ae = a.size(), bb = 0, be = b.size();
  while (ab < ae && isspace((unsigned char)a[ab]))
    ab++;
  while (ae > ab && isspace((unsigned char)a[ae - 1]))
    ae--;
  while (bb < be && isspace((unsigned char)b[bb]))
    bb++;
  while (be > bb && isspace((unsigned char)b[be - 1]))
    be--;
  return ae - ab == be - bb && a.compare(ab, ae - ab, b, bb, be - bb) == 0;
}

std::string PatchNoteJson(const std::string &note) {
  return "{\"note\":" + JsonString(Truncate(note, kMaxNoteChars)) + "}";
}

std::string TagJson(const std::string &name) {
  return "{\"name\":" + JsonString(name) + "}";
}

std::string CleanToken(const std::string &raw) {
  std::string out;
  for (size_t i = 0; i < raw.size(); i++) {
    const unsigned char c = (unsigned char)raw[i];
    if (c > 0x20 && c < 0x7F)
      out.push_back((char)c);
  }
  return out;
}

} // namespace readwise_api_utils
