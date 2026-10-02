/*
    3dslibris - word_lookup_utils.cpp

    See include/dictionary/word_lookup_utils.h.
*/

#include "dictionary/word_lookup_utils.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8proc.h"

namespace word_lookup_utils {

namespace {

std::vector<int32_t> Decode(const std::string &s) {
  std::vector<int32_t> out;
  const utf8proc_uint8_t *p = (const utf8proc_uint8_t *)s.data();
  utf8proc_ssize_t left = (utf8proc_ssize_t)s.size();
  while (left > 0) {
    int32_t cp = 0;
    utf8proc_ssize_t n = utf8proc_iterate(p, left, &cp);
    if (n <= 0) {
      // Invalid byte: skip it.
      n = 1;
      cp = -1;
    }
    if (cp >= 0)
      out.push_back(cp);
    p += n;
    left -= n;
  }
  return out;
}

void AppendUtf8(std::string *out, int32_t cp) {
  utf8proc_uint8_t buf[4];
  const utf8proc_ssize_t n = utf8proc_encode_char(cp, buf);
  out->append((const char *)buf, (size_t)n);
}

std::string Encode(const std::vector<int32_t> &cps, size_t begin, size_t end) {
  std::string out;
  for (size_t i = begin; i < end; i++)
    AppendUtf8(&out, cps[i]);
  return out;
}

bool IsWordChar(int32_t cp) {
  switch (utf8proc_category(cp)) {
  case UTF8PROC_CATEGORY_LU:
  case UTF8PROC_CATEGORY_LL:
  case UTF8PROC_CATEGORY_LT:
  case UTF8PROC_CATEGORY_LM:
  case UTF8PROC_CATEGORY_LO:
  case UTF8PROC_CATEGORY_ND:
  case UTF8PROC_CATEGORY_NL:
  case UTF8PROC_CATEGORY_NO:
    return true;
  default:
    return false;
  }
}

bool EndsWith(const std::string &s, const char *suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool IsVowel(char c) {
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

void AddUnique(std::vector<std::string> *out, const std::string &word,
               const std::string &form) {
  if (form.size() < 2 || form == word)
    return;
  for (size_t i = 0; i < out->size(); i++)
    if ((*out)[i] == form)
      return;
  out->push_back(form);
}

bool IsBlockTag(const std::string &name) {
  static const char *kBlocks[] = {"p",  "div", "li", "tr", "h1", "h2",
                                  "h3", "h4",  "h5", "h6", "dd", "dt",
                                  "ol", "ul",  "blockquote", "table"};
  for (size_t i = 0; i < sizeof(kBlocks) / sizeof(kBlocks[0]); i++)
    if (name == kBlocks[i])
      return true;
  return false;
}

// Decodes the entity starting at s[i] ('&'); returns its length, 0 if it
// isn't one.
size_t DecodeEntity(const std::string &s, size_t i, std::string *out) {
  const size_t semi = s.find(';', i);
  if (semi == std::string::npos || semi - i > 10)
    return 0;
  const std::string name = s.substr(i + 1, semi - i - 1);
  int32_t cp = -1;
  if (name == "amp")
    cp = '&';
  else if (name == "lt")
    cp = '<';
  else if (name == "gt")
    cp = '>';
  else if (name == "quot")
    cp = '"';
  else if (name == "apos")
    cp = '\'';
  else if (name == "nbsp")
    cp = ' ';
  else if (name == "mdash")
    cp = 0x2014;
  else if (name == "ndash")
    cp = 0x2013;
  else if (name.size() > 1 && name[0] == '#') {
    char *end = NULL;
    const long v = (name[1] == 'x' || name[1] == 'X')
                       ? strtol(name.c_str() + 2, &end, 16)
                       : strtol(name.c_str() + 1, &end, 10);
    if (end && *end == '\0' && v > 0 && v <= 0x10FFFF)
      cp = (int32_t)v;
  }
  if (cp < 0)
    return 0;
  AppendUtf8(out, cp);
  return semi - i + 1;
}

} // namespace

std::string CleanSelectedWord(const std::string &utf8) {
  std::vector<int32_t> cps;
  const std::vector<int32_t> raw = Decode(utf8);
  for (size_t i = 0; i < raw.size(); i++) {
    int32_t cp = raw[i];
    if (cp == 0x00AD || cp == 0x200B || cp == 0x200C || cp == 0x200D ||
        cp == 0xFEFF)
      continue;
    if (cp == 0x2019 || cp == 0x2018 || cp == 0x02BC)
      cp = '\'';
    cps.push_back(cp);
  }
  size_t begin = 0;
  size_t end = cps.size();
  while (begin < end && !IsWordChar(cps[begin]))
    begin++;
  while (end > begin && !IsWordChar(cps[end - 1]))
    end--;
  return Encode(cps, begin, end);
}

std::string JoinHyphenated(const std::string &first,
                           const std::string &second) {
  std::string head = first;
  // Trailing soft hyphen (U+00AD) or hyphen-minus / Unicode hyphen.
  if (EndsWith(head, "\xC2\xAD"))
    head.erase(head.size() - 2);
  else if (EndsWith(head, "\xE2\x80\x90"))
    head.erase(head.size() - 3);
  else if (EndsWith(head, "-"))
    head.erase(head.size() - 1);
  else
    return std::string();
  return head + second;
}

std::string ToLower(const std::string &utf8) {
  const std::vector<int32_t> cps = Decode(utf8);
  std::string out;
  for (size_t i = 0; i < cps.size(); i++)
    AppendUtf8(&out, utf8proc_tolower(cps[i]));
  return out;
}

std::vector<std::string> BaseFormCandidates(const std::string &lower_word) {
  std::vector<std::string> out;
  std::string word = lower_word;
  // Possessives: "dog's" -> "dog", "dogs'" -> "dogs" (then "dog").
  if (EndsWith(word, "'s")) {
    word.erase(word.size() - 2);
    AddUnique(&out, lower_word, word);
  } else if (EndsWith(word, "s'")) {
    word.erase(word.size() - 1);
    AddUnique(&out, lower_word, word);
  }

  // WordNet morphy's detachment rules (noun, verb, adjective), plus
  // adverbs in -ly.
  static const char *kRules[][2] = {
      {"ies", "y"}, {"ches", "ch"}, {"shes", "sh"}, {"sses", "ss"},
      {"ses", "s"}, {"xes", "x"},   {"zes", "z"},   {"men", "man"},
      {"es", "e"},  {"es", ""},     {"s", ""},      {"ied", "y"},
      {"ed", "e"},  {"ed", ""},     {"ying", "ie"}, {"ing", "e"},
      {"ing", ""},  {"iest", "y"},  {"ier", "y"},   {"est", "e"},
      {"est", ""},  {"er", "e"},    {"er", ""},     {"ily", "y"},
      {"ly", ""},
  };
  for (size_t r = 0; r < sizeof(kRules) / sizeof(kRules[0]); r++) {
    const char *suffix = kRules[r][0];
    if (!EndsWith(word, suffix) || word.size() <= strlen(suffix))
      continue;
    const std::string stem = word.substr(0, word.size() - strlen(suffix));
    if (EndsWith(word, "ss") && strcmp(suffix, "s") == 0)
      continue; // "glass" is not a plural
    AddUnique(&out, lower_word, stem + kRules[r][1]);
    // "running" -> "runn" -> "run", "bigger" -> "bigg" -> "big".
    if (kRules[r][1][0] == '\0' && stem.size() >= 3) {
      const char last = stem[stem.size() - 1];
      if (last == stem[stem.size() - 2] && isalpha((unsigned char)last) &&
          !IsVowel(last) && last != 's' && last != 'l')
        AddUnique(&out, lower_word, stem.substr(0, stem.size() - 1));
    }
  }
  return out;
}

std::string MarkupToText(const std::string &markup) {
  std::string out;
  bool pending_space = false;
  size_t skip_until = std::string::npos; // inside <script>/<style>
  for (size_t i = 0; i < markup.size();) {
    const char c = markup[i];
    if (c == '<') {
      const size_t close = markup.find('>', i);
      if (close == std::string::npos)
        break;
      std::string tag = markup.substr(i + 1, close - i - 1);
      i = close + 1;
      const bool closing = !tag.empty() && tag[0] == '/';
      size_t n = closing ? 1 : 0;
      std::string name;
      while (n < tag.size() && (isalnum((unsigned char)tag[n])))
        name += (char)tolower((unsigned char)tag[n++]);
      if (skip_until != std::string::npos) {
        if (closing && (name == "script" || name == "style"))
          skip_until = std::string::npos;
        continue;
      }
      if (!closing && (name == "script" || name == "style")) {
        skip_until = 0;
        continue;
      }
      // An empty list item (Wiktionary has them) leaves no bare bullet.
      if (closing && EndsWith(out, "\xE2\x80\xA2 "))
        out.erase(out.size() - 4);
      // One line break per block boundary, however many tags meet there.
      const bool at_line_start = out.empty() || out[out.size() - 1] == '\n';
      if (name == "br" || (IsBlockTag(name) && (closing || name != "li"))) {
        if (!at_line_start)
          out += '\n';
        pending_space = false;
      } else if (name == "li" && !closing) {
        if (!at_line_start)
          out += '\n';
        out += "\xE2\x80\xA2 ";
        pending_space = false;
      }
      continue;
    }
    if (skip_until != std::string::npos) {
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pending_space = true;
      i++;
      continue;
    }
    if (pending_space && !out.empty() && out[out.size() - 1] != '\n' &&
        out[out.size() - 1] != ' ')
      out += ' ';
    pending_space = false;
    if (c == '&') {
      const size_t used = DecodeEntity(markup, i, &out);
      if (used) {
        i += used;
        continue;
      }
    }
    out += c;
    i++;
  }
  return TidyText(out);
}

std::string TidyText(const std::string &text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(start, end - start);
    size_t b = 0;
    size_t e = line.size();
    while (b < e && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r'))
      b++;
    while (e > b &&
           (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r'))
      e--;
    lines.push_back(line.substr(b, e - b));
    start = end + 1;
  }
  std::string out;
  bool last_empty = true; // also drops leading empty lines
  for (size_t i = 0; i < lines.size(); i++) {
    const bool empty = lines[i].empty();
    if (empty && last_empty)
      continue;
    if (!out.empty())
      out += '\n';
    out += lines[i];
    last_empty = empty;
  }
  while (!out.empty() && out[out.size() - 1] == '\n')
    out.erase(out.size() - 1);
  return out;
}

namespace {

// Length in codepoints of a line's hanging prefix: leading spaces, then
// "12. " or "• ".
size_t HangingPrefix(const std::vector<int32_t> &cps) {
  size_t i = 0;
  while (i < cps.size() && cps[i] == ' ')
    i++;
  size_t j = i;
  while (j < cps.size() && cps[j] >= '0' && cps[j] <= '9')
    j++;
  if (j > i && j + 1 < cps.size() && (cps[j] == '.' || cps[j] == ')') &&
      cps[j + 1] == ' ')
    return j + 2;
  if (i + 1 < cps.size() && cps[i] == 0x2022 && cps[i + 1] == ' ')
    return i + 2;
  return i;
}

} // namespace

std::vector<WrappedLine> WrapText(const std::string &text, int max_width_px,
                                  AdvanceFn advance, void *ctx) {
  std::vector<WrappedLine> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    const std::vector<int32_t> cps = Decode(text.substr(start, end - start));
    start = end + 1;
    if (cps.empty()) {
      WrappedLine empty = {std::string(), 0};
      lines.push_back(empty);
      continue;
    }
    std::vector<int> widths(cps.size());
    for (size_t i = 0; i < cps.size(); i++)
      widths[i] = advance(ctx, (uint32_t)cps[i]);
    int hang = 0;
    const size_t prefix = HangingPrefix(cps);
    for (size_t i = 0; i < prefix; i++)
      hang += widths[i];
    if (hang > max_width_px / 3)
      hang = 0;

    size_t line_start = 0;
    int indent = 0;
    while (line_start < cps.size()) {
      const int room = max_width_px - indent;
      int width = 0;
      size_t i = line_start;
      size_t last_space = std::string::npos;
      while (i < cps.size() && (width + widths[i] <= room || i == line_start)) {
        if (cps[i] == ' ')
          last_space = i;
        width += widths[i];
        i++;
      }
      size_t line_end = i;
      size_t next = i;
      if (i < cps.size() && cps[i] != ' ' && last_space != std::string::npos &&
          last_space > line_start) {
        line_end = last_space;
        next = last_space + 1;
      }
      while (next < cps.size() && cps[next] == ' ')
        next++;
      WrappedLine line = {Encode(cps, line_start, line_end), indent};
      lines.push_back(line);
      line_start = next;
      indent = hang;
    }
  }
  return lines;
}

std::string UrlEncode(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    const unsigned char c = (unsigned char)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string WiktionaryDefinitionUrl(const std::string &word) {
  return "https://en.wiktionary.org/api/rest_v1/page/definition/" +
         UrlEncode(word) + "?redirect=true";
}

std::string WikipediaSummaryUrl(const std::string &word) {
  std::string title = word;
  for (size_t i = 0; i < title.size(); i++)
    if (title[i] == ' ')
      title[i] = '_';
  return "https://en.wikipedia.org/api/rest_v1/page/summary/" +
         UrlEncode(title) + "?redirect=true";
}

} // namespace word_lookup_utils
