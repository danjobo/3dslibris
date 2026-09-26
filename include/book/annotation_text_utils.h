/*
    3dslibris - annotation_text_utils.h

    Pure helpers for text-anchored highlights: turn a parsed page buffer into
    its visible characters (with a map back to buffer indices), build a
    quote/prefix anchor for a selected buffer range, and re-find that anchor
    after the book has been reflowed.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace annotation_text_utils {

// Visible characters of a page buffer. buf_index[i] is the index in the page
// buffer of chars[i]; synthetic separators (images, screen breaks) map to the
// token that produced them.
struct VisibleText {
  std::vector<uint32_t> chars;
  std::vector<int> buf_index;
};

// Skips style/layout tokens and their arguments exactly as Page::Draw does.
void ExtractVisibleText(const uint32_t *buf, int len, VisibleText *out);

// Collapses whitespace runs (spaces, line breaks, NBSP, ...) into a single
// ' ' and drops invisible soft hyphens / zero-width spaces, so text matches
// regardless of where lines were wrapped.
void NormalizeVisibleText(const VisibleText &in, VisibleText *out);

std::string CodepointsToUtf8(const std::vector<uint32_t> &cps, size_t begin,
                             size_t end);
std::vector<uint32_t> Utf8ToCodepoints(const std::string &s);

// Builds the anchor for buffer range [buf_begin, buf_end) of a page. The
// quote is capped at max_quote_chars codepoints. Returns false when the range
// contains no visible text.
bool BuildAnchorFromBufferRange(const uint32_t *buf, int len, int buf_begin,
                                int buf_end, size_t max_quote_chars,
                                size_t prefix_chars, std::string *quote,
                                std::string *prefix);

// Proportional page remap used to center the anchor search after reflow.
int RemapPageHint(int page_hint, int page_count_hint, int page_count);

struct ResolvedSpan {
  int page;
  int buf_begin; // inclusive page-buffer index
  int buf_end;   // exclusive page-buffer index
};

// Returns the buffer of a page; false if the page is unavailable.
typedef bool (*PageBufferFn)(void *ctx, int page, const uint32_t **buf,
                             int *len);

// Finds the quote in the book, preferring pages near the remapped hint and
// occurrences whose preceding text matches the stored prefix. A quote may
// cross into the following page, producing two spans. Searches +/- window
// pages first, then (if whole_book_fallback) the rest of the book.
bool ResolveAnchor(const std::string &quote, const std::string &prefix,
                   int page_hint, int page_count_hint, int page_count,
                   PageBufferFn page_buffer, void *ctx, int window,
                   std::vector<ResolvedSpan> *out,
                   bool whole_book_fallback = true);

} // namespace annotation_text_utils
