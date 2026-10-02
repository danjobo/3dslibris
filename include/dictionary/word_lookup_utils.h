/*
    3dslibris - word_lookup_utils.h

    Pure helpers for looking up a word picked on the page: cleaning the
    picked text, base forms to try when the word itself isn't in the
    dictionary ("running" -> "run"), markup to plain text, and URL pieces
    for the online lookup.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace word_lookup_utils {

// Trims punctuation and quotes around a word as drawn on the page
// ("“Dog,”" -> "Dog"), turns curly apostrophes into ' and drops
// soft hyphens and zero-width spaces. Empty if no letter or digit is left.
std::string CleanSelectedWord(const std::string &utf8);

// A word broken over two lines by hyphenation: "exam-" + "ple" ->
// "example". Empty if first doesn't end with a hyphen.
std::string JoinHyphenated(const std::string &first, const std::string &second);

// Unicode lower case.
std::string ToLower(const std::string &utf8);

// Forms to try after the (lower-case) word itself, most likely first:
// possessives and the regular English inflections WordNet's morphy undoes
// ("dogs" -> "dog", "ponies" -> "pony", "running" -> "run",
// "happily" -> "happy"). Never contains the word itself.
std::vector<std::string> BaseFormCandidates(const std::string &lower_word);

// HTML (or Pango/XDXF) to plain text: tags dropped, block ends and <br> as
// line breaks, list items as "• ", entities decoded, whitespace
// collapsed.
std::string MarkupToText(const std::string &markup);

// Trims each line, drops spaces before line breaks, and keeps at most one
// empty line in a row.
std::string TidyText(const std::string &text);

// Word-wraps text into lines at most max_width_px wide, breaking at spaces
// (or anywhere in a word too long for a line). Lines continuing a numbered
// sense ("2. ...") or a bullet line are indented under its text.
struct WrappedLine {
  std::string text;
  int indent_px;
};
typedef int (*AdvanceFn)(void *ctx, uint32_t codepoint);
std::vector<WrappedLine> WrapText(const std::string &text, int max_width_px,
                                  AdvanceFn advance, void *ctx);

// Percent-encodes everything but unreserved URL characters.
std::string UrlEncode(const std::string &s);

// REST URLs for the online lookup.
std::string WiktionaryDefinitionUrl(const std::string &word);
std::string WikipediaSummaryUrl(const std::string &word);

} // namespace word_lookup_utils
