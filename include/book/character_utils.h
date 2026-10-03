/*
    3dslibris - character_utils.h

    Pure helpers for the character list: comparing saved names, and finding
    where each name is mentioned in the book (whole words, ignoring case)
    with the sentence around each mention as a snippet.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "book/annotation_text_utils.h"

namespace character_utils {

// The name as compared: lower case, curly apostrophes as ', whitespace
// runs as one space, trimmed. Two saved names with the same key are the
// same character.
std::string NameKey(const std::string &name);

struct Mention {
  int page;
  std::string snippet; // the sentence around the mention, "..." where cut
};

// Mentions of each name on pages 0..last_page, in page order, at most
// max_per_name each. A name matches as whole words ignoring case
// ("Hughes" matches "HUGHES," and "Hughes's", not "Hughesville").
// out gets one list per name.
void FindMentions(const std::vector<std::string> &names,
                  annotation_text_utils::PageBufferFn page_buffer, void *ctx,
                  int last_page, size_t max_per_name,
                  std::vector<std::vector<Mention> > *out);

// Where the name appears on one page, as page-buffer ranges
// [buf_begin, buf_end), matched like FindMentions.
struct BufRange {
  int buf_begin;
  int buf_end;
};
void FindOnPage(const std::string &name, const uint32_t *buf, int len,
                std::vector<BufRange> *out);

// The sentence around chars[begin, end): back to just after the previous
// '.', '!' or '?' and on to the next one, at most max_side codepoints
// either way ("..." added where cut). UTF-8.
std::string SentenceAround(const std::vector<uint32_t> &chars, size_t begin,
                           size_t end, size_t max_side);

} // namespace character_utils
