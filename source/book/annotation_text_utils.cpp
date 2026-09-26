#include "book/annotation_text_utils.h"

#include <algorithm>
#include <stdlib.h>

#include "shared/text_token_constants.h"

namespace annotation_text_utils {

namespace {

bool IsCollapsibleSpace(uint32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x3000;
}

bool IsInvisible(uint32_t c) { return c == 0xAD || c == 0x200B; }

void Emit(VisibleText *out, uint32_t c, int index) {
  out->chars.push_back(c);
  out->buf_index.push_back(index);
}

// A normalized page plus, optionally, the page after it, so quotes that a
// relayout pushed across a page break can still be found.
struct Window {
  std::vector<uint32_t> chars;
  std::vector<int> page;      // -1 for the synthetic join separator
  std::vector<int> buf_index;
  size_t first_page_len;
};

bool LoadNormalizedPage(PageBufferFn page_buffer, void *ctx, int page,
                        VisibleText *out) {
  out->chars.clear();
  out->buf_index.clear();
  const uint32_t *buf = NULL;
  int len = 0;
  if (!page_buffer || !page_buffer(ctx, page, &buf, &len) || !buf || len <= 0)
    return false;
  VisibleText raw;
  ExtractVisibleText(buf, len, &raw);
  NormalizeVisibleText(raw, out);
  return true;
}

void AppendToWindow(Window *w, const VisibleText &text, int page) {
  for (size_t i = 0; i < text.chars.size(); i++) {
    w->chars.push_back(text.chars[i]);
    w->page.push_back(page);
    w->buf_index.push_back(text.buf_index[i]);
  }
}

size_t PrefixScore(const Window &w, size_t start,
                   const std::vector<uint32_t> &prefix) {
  size_t score = 0;
  size_t wi = start;
  // Prefixes are stored trimmed; skip the space separating them from the
  // quote before comparing backwards.
  while (wi > 0 && w.chars[wi - 1] == ' ')
    wi--;
  size_t pi = prefix.size();
  while (wi > 0 && pi > 0) {
    uint32_t a = w.chars[wi - 1];
    uint32_t b = prefix[pi - 1];
    if (a != b)
      break;
    score++;
    wi--;
    pi--;
  }
  return score;
}

struct Candidate {
  bool found;
  size_t score;
  int distance;
  std::vector<ResolvedSpan> spans;
  Candidate() : found(false), score(0), distance(0) {}
};

void SpansForMatch(const Window &w, size_t start, size_t end,
                   std::vector<ResolvedSpan> *spans) {
  spans->clear();
  for (size_t i = start; i < end; i++) {
    const int page = w.page[i];
    if (page < 0)
      continue;
    if (spans->empty() || spans->back().page != page) {
      ResolvedSpan span;
      span.page = page;
      span.buf_begin = w.buf_index[i];
      span.buf_end = w.buf_index[i] + 1;
      spans->push_back(span);
    } else {
      spans->back().buf_end = w.buf_index[i] + 1;
    }
  }
}

void SearchPage(int page, int page_count, int center,
                const std::vector<uint32_t> &quote,
                const std::vector<uint32_t> &prefix, PageBufferFn page_buffer,
                void *ctx, Candidate *best) {
  VisibleText first;
  if (!LoadNormalizedPage(page_buffer, ctx, page, &first) ||
      first.chars.empty())
    return;

  Window w;
  AppendToWindow(&w, first, page);
  w.first_page_len = w.chars.size();
  if (page + 1 < page_count) {
    VisibleText next;
    if (LoadNormalizedPage(page_buffer, ctx, page + 1, &next) &&
        !next.chars.empty()) {
      const bool need_space = w.chars.back() != ' ' && next.chars[0] != ' ';
      if (need_space) {
        w.chars.push_back(' ');
        w.page.push_back(-1);
        w.buf_index.push_back(-1);
      }
      AppendToWindow(&w, next, page + 1);
    }
  }

  if (w.chars.size() < quote.size())
    return;
  const int distance = abs(page - center);
  for (size_t s = 0; s < w.first_page_len; s++) {
    if (s + quote.size() > w.chars.size())
      break;
    if (!std::equal(quote.begin(), quote.end(), w.chars.begin() + s))
      continue;
    const size_t score = PrefixScore(w, s, prefix);
    const bool better =
        !best->found || score > best->score ||
        (score == best->score && distance < best->distance);
    if (!better)
      continue;
    best->found = true;
    best->score = score;
    best->distance = distance;
    SpansForMatch(w, s, s + quote.size(), &best->spans);
  }
}

void TrimSpaces(VisibleText *text) {
  size_t begin = 0;
  size_t end = text->chars.size();
  while (begin < end && text->chars[begin] == ' ')
    begin++;
  while (end > begin && text->chars[end - 1] == ' ')
    end--;
  text->chars.assign(text->chars.begin() + begin, text->chars.begin() + end);
  text->buf_index.assign(text->buf_index.begin() + begin,
                         text->buf_index.begin() + end);
}

std::vector<uint32_t> NormalizeCodepoints(const std::vector<uint32_t> &cps) {
  VisibleText raw;
  raw.chars = cps;
  raw.buf_index.assign(cps.size(), 0);
  VisibleText norm;
  NormalizeVisibleText(raw, &norm);
  TrimSpaces(&norm);
  return norm.chars;
}

} // namespace

void ExtractVisibleText(const uint32_t *buf, int len, VisibleText *out) {
  if (!out)
    return;
  out->chars.clear();
  out->buf_index.clear();
  if (!buf || len <= 0)
    return;

  int i = 0;
  while (i < len) {
    const uint32_t c = buf[i];
    if (c >= 0x110000) {
      switch (c) {
      case TEXT_LINK_START:
      case TEXT_IMAGE_ALIGN:
      case TEXT_IMAGE_AUTHOR_WIDTH:
      case TEXT_LINE_START_X:
      case TEXT_FONT_SIZE:
        i += 2;
        break;
      case TEXT_HR_BOUNDS:
        Emit(out, '\n', i);
        i += 3;
        break;
      case TEXT_SCREEN_BREAK:
        Emit(out, '\n', i);
        i++;
        break;
      default:
        i++;
        break;
      }
      continue;
    }
    if (c < 0x20) {
      switch (c) {
      case TEXT_IMAGE:
        Emit(out, '\n', i);
        i += 2;
        break;
      case TEXT_RTL_LINE_PX:
      case TEXT_UNDERLINE_STYLE:
        i += 2;
        break;
      case '\n':
      case '\r':
      case '\t':
      case TEXT_HR:
        Emit(out, '\n', i);
        i++;
        break;
      default:
        i++;
        break;
      }
      continue;
    }
    Emit(out, c, i);
    i++;
  }
}

void NormalizeVisibleText(const VisibleText &in, VisibleText *out) {
  if (!out)
    return;
  out->chars.clear();
  out->buf_index.clear();
  bool prev_space = false;
  for (size_t i = 0; i < in.chars.size(); i++) {
    const uint32_t c = in.chars[i];
    if (IsInvisible(c))
      continue;
    if (IsCollapsibleSpace(c)) {
      if (!prev_space)
        Emit(out, ' ', in.buf_index[i]);
      prev_space = true;
      continue;
    }
    Emit(out, c, in.buf_index[i]);
    prev_space = false;
  }
}

std::string CodepointsToUtf8(const std::vector<uint32_t> &cps, size_t begin,
                             size_t end) {
  std::string out;
  end = std::min(end, cps.size());
  for (size_t i = begin; i < end; i++) {
    const uint32_t c = cps[i];
    if (c < 0x80) {
      out.push_back((char)c);
    } else if (c < 0x800) {
      out.push_back((char)(0xC0 | (c >> 6)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back((char)(0xE0 | (c >> 12)));
      out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    } else if (c < 0x110000) {
      out.push_back((char)(0xF0 | (c >> 18)));
      out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
      out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

std::vector<uint32_t> Utf8ToCodepoints(const std::string &s) {
  std::vector<uint32_t> out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char b0 = (unsigned char)s[i];
    uint32_t c = 0xFFFD;
    size_t n = 1;
    if (b0 < 0x80) {
      c = b0;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
      c = ((uint32_t)(b0 & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
      n = 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
      c = ((uint32_t)(b0 & 0x0F) << 12) |
          ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 6) |
          ((unsigned char)s[i + 2] & 0x3F);
      n = 3;
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
      c = ((uint32_t)(b0 & 0x07) << 18) |
          ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 12) |
          ((uint32_t)((unsigned char)s[i + 2] & 0x3F) << 6) |
          ((unsigned char)s[i + 3] & 0x3F);
      n = 4;
    }
    out.push_back(c);
    i += n;
  }
  return out;
}

bool BuildAnchorFromBufferRange(const uint32_t *buf, int len, int buf_begin,
                                int buf_end, size_t max_quote_chars,
                                size_t prefix_chars, std::string *quote,
                                std::string *prefix) {
  if (!quote || !prefix || buf_end <= buf_begin)
    return false;
  VisibleText raw;
  ExtractVisibleText(buf, len, &raw);
  VisibleText norm;
  NormalizeVisibleText(raw, &norm);

  size_t first = norm.chars.size();
  size_t last = 0;
  for (size_t i = 0; i < norm.chars.size(); i++) {
    const int bi = norm.buf_index[i];
    if (bi < buf_begin || bi >= buf_end)
      continue;
    if (first == norm.chars.size())
      first = i;
    last = i + 1;
  }
  while (first < last && norm.chars[first] == ' ')
    first++;
  while (last > first && norm.chars[last - 1] == ' ')
    last--;
  if (first >= last)
    return false;
  if (max_quote_chars > 0 && last - first > max_quote_chars)
    last = first + max_quote_chars;
  while (last > first && norm.chars[last - 1] == ' ')
    last--;

  size_t prefix_end = first;
  while (prefix_end > 0 && norm.chars[prefix_end - 1] == ' ')
    prefix_end--;
  size_t prefix_begin =
      prefix_end > prefix_chars ? prefix_end - prefix_chars : 0;
  while (prefix_begin < prefix_end && norm.chars[prefix_begin] == ' ')
    prefix_begin++;

  *quote = CodepointsToUtf8(norm.chars, first, last);
  *prefix = CodepointsToUtf8(norm.chars, prefix_begin, prefix_end);
  return true;
}

int RemapPageHint(int page_hint, int page_count_hint, int page_count) {
  if (page_count <= 0)
    return 0;
  int page = page_hint;
  if (page_count_hint > 0 && page_count_hint != page_count)
    page = (int)(((long long)page_hint * page_count + page_count_hint / 2) /
                 page_count_hint);
  if (page < 0)
    page = 0;
  if (page >= page_count)
    page = page_count - 1;
  return page;
}

bool ResolveAnchor(const std::string &quote, const std::string &prefix,
                   int page_hint, int page_count_hint, int page_count,
                   PageBufferFn page_buffer, void *ctx, int window,
                   std::vector<ResolvedSpan> *out, bool whole_book_fallback) {
  if (!out)
    return false;
  out->clear();
  if (page_count <= 0 || !page_buffer)
    return false;
  const std::vector<uint32_t> quote_cps =
      NormalizeCodepoints(Utf8ToCodepoints(quote));
  if (quote_cps.empty())
    return false;
  const std::vector<uint32_t> prefix_cps =
      NormalizeCodepoints(Utf8ToCodepoints(prefix));

  const int center = RemapPageHint(page_hint, page_count_hint, page_count);
  if (window < 0)
    window = 0;
  Candidate best;
  const int lo = std::max(0, center - window);
  const int hi = std::min(page_count - 1, center + window);
  for (int page = lo; page <= hi; page++)
    SearchPage(page, page_count, center, quote_cps, prefix_cps, page_buffer,
               ctx, &best);

  if (!best.found && whole_book_fallback) {
    for (int page = 0; page < page_count; page++) {
      if (page >= lo && page <= hi)
        continue;
      SearchPage(page, page_count, center, quote_cps, prefix_cps, page_buffer,
                 ctx, &best);
    }
  }

  if (!best.found)
    return false;
  *out = best.spans;
  return true;
}

} // namespace annotation_text_utils
