/*
    3dslibris - character_utils.cpp

    See include/book/character_utils.h.
*/

#include "book/character_utils.h"

#include "utf8proc.h"

namespace character_utils {

namespace {

uint32_t Fold(uint32_t c) {
  if (c == 0x2019 || c == 0x2018 || c == 0x02BC)
    return '\'';
  return (uint32_t)utf8proc_tolower((utf8proc_int32_t)c);
}

bool IsWordChar(uint32_t c) {
  switch (utf8proc_category((utf8proc_int32_t)c)) {
  case UTF8PROC_CATEGORY_LU:
  case UTF8PROC_CATEGORY_LL:
  case UTF8PROC_CATEGORY_LT:
  case UTF8PROC_CATEGORY_LM:
  case UTF8PROC_CATEGORY_LO:
  case UTF8PROC_CATEGORY_MN:
  case UTF8PROC_CATEGORY_ND:
    return true;
  default:
    return false;
  }
}

bool IsSpace(uint32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0;
}

// Whether chars[i] ends a sentence: '!' or '?', or a '.' followed by a
// space (not "3.5" or "U.S.A") that doesn't end a short capitalized
// abbreviation such as "Mr." or "Sgt.".
bool IsSentenceEnd(const std::vector<uint32_t> &chars, size_t i) {
  const uint32_t c = chars[i];
  if (c == '!' || c == '?')
    return true;
  if (c != '.')
    return false;
  // A closing quote or bracket may follow it ("Go." he said).
  size_t next = i + 1;
  while (next < chars.size() && !IsSpace(chars[next]) &&
         !IsWordChar(chars[next]))
    next++;
  if (next < chars.size() && !IsSpace(chars[next]))
    return false;
  size_t word = i;
  while (word > 0 && IsWordChar(chars[word - 1]))
    word--;
  const size_t letters = i - word;
  const bool capitalized =
      letters > 0 && utf8proc_category((utf8proc_int32_t)chars[word]) ==
                         UTF8PROC_CATEGORY_LU;
  return !(capitalized && letters <= 3);
}

std::vector<uint32_t> KeyCodepoints(const std::string &name) {
  return annotation_text_utils::Utf8ToCodepoints(NameKey(name));
}

} // namespace

std::string NameKey(const std::string &name) {
  const std::vector<uint32_t> cps =
      annotation_text_utils::Utf8ToCodepoints(name);
  std::vector<uint32_t> out;
  bool pending_space = false;
  for (size_t i = 0; i < cps.size(); i++) {
    if (IsSpace(cps[i])) {
      pending_space = !out.empty();
      continue;
    }
    if (cps[i] == 0xAD || cps[i] == 0x200B)
      continue;
    if (pending_space)
      out.push_back(' ');
    pending_space = false;
    out.push_back(Fold(cps[i]));
  }
  return annotation_text_utils::CodepointsToUtf8(out, 0, out.size());
}

std::string SentenceAround(const std::vector<uint32_t> &chars, size_t begin,
                           size_t end, size_t max_side) {
  if (begin > end || end > chars.size())
    return std::string();
  size_t start = begin;
  bool cut_start = false;
  while (start > 0) {
    if (start < begin && IsSentenceEnd(chars, start - 1)) {
      // Not the previous sentence's closing quote or bracket.
      while (start < begin && !IsSpace(chars[start]) &&
             !IsWordChar(chars[start]))
        start++;
      break;
    }
    if (begin - start >= max_side) {
      cut_start = true;
      break;
    }
    start--;
  }
  size_t stop = end;
  bool cut_end = false;
  while (stop < chars.size()) {
    if (IsSentenceEnd(chars, stop)) {
      stop++;
      // Keep a closing quote or bracket with its sentence.
      while (stop < chars.size() && !IsSpace(chars[stop]) &&
             !IsWordChar(chars[stop]))
        stop++;
      break;
    }
    if (stop - end >= max_side) {
      cut_end = true;
      break;
    }
    stop++;
  }
  // Cut at word boundaries, and skip the spaces at either end.
  if (cut_start)
    while (start < begin && !IsSpace(chars[start]))
      start++;
  if (cut_end)
    while (stop > end && !IsSpace(chars[stop - 1]))
      stop--;
  while (start < begin && IsSpace(chars[start]))
    start++;
  while (stop > end && IsSpace(chars[stop - 1]))
    stop--;
  std::string out = cut_start ? "..." : "";
  out += annotation_text_utils::CodepointsToUtf8(chars, start, stop);
  if (cut_end)
    out += "...";
  return out;
}

void FindMentions(const std::vector<std::string> &names,
                  annotation_text_utils::PageBufferFn page_buffer, void *ctx,
                  int last_page, size_t max_per_name,
                  std::vector<std::vector<Mention> > *out) {
  out->assign(names.size(), std::vector<Mention>());
  std::vector<std::vector<uint32_t> > keys;
  for (size_t n = 0; n < names.size(); n++)
    keys.push_back(KeyCodepoints(names[n]));

  for (int page = 0; page <= last_page; page++) {
    const uint32_t *buf = NULL;
    int len = 0;
    if (!page_buffer(ctx, page, &buf, &len) || !buf || len <= 0)
      continue;
    annotation_text_utils::VisibleText raw;
    annotation_text_utils::VisibleText text;
    annotation_text_utils::ExtractVisibleText(buf, len, &raw);
    annotation_text_utils::NormalizeVisibleText(raw, &text);
    const std::vector<uint32_t> &chars = text.chars;
    std::vector<uint32_t> folded(chars.size());
    for (size_t i = 0; i < chars.size(); i++)
      folded[i] = Fold(chars[i]);

    for (size_t i = 0; i < folded.size(); i++) {
      // Names start at a word start.
      if (i > 0 && IsWordChar(chars[i - 1]))
        continue;
      for (size_t n = 0; n < keys.size(); n++) {
        const std::vector<uint32_t> &key = keys[n];
        if (key.empty() || (*out)[n].size() >= max_per_name ||
            i + key.size() > folded.size() || folded[i] != key[0])
          continue;
        size_t k = 1;
        while (k < key.size() && folded[i + k] == key[k])
          k++;
        if (k < key.size())
          continue;
        const size_t end = i + key.size();
        if (end < chars.size() && IsWordChar(chars[end]))
          continue;
        Mention m;
        m.page = page;
        m.snippet = SentenceAround(chars, i, end, 160);
        (*out)[n].push_back(m);
      }
    }
  }
}

} // namespace character_utils
