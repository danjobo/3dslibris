#include "book/annotation_text_utils.h"

#include "shared/text_token_constants.h"
#include "test_assert.h"

#include <string>
#include <vector>

using annotation_text_utils::ResolvedSpan;
using annotation_text_utils::VisibleText;

namespace {

void Append(std::vector<uint32_t> *buf, const char *text) {
  for (const char *p = text; *p; ++p)
    buf->push_back((unsigned char)*p);
}

std::vector<uint32_t> Buf(const char *text) {
  std::vector<uint32_t> buf;
  Append(&buf, text);
  return buf;
}

std::string VisibleUtf8(const VisibleText &text) {
  return annotation_text_utils::CodepointsToUtf8(text.chars, 0,
                                                 text.chars.size());
}

struct Pages {
  std::vector<std::vector<uint32_t> > pages;
};

bool PageBuffer(void *ctx, int page, const uint32_t **buf, int *len) {
  Pages *p = (Pages *)ctx;
  if (page < 0 || page >= (int)p->pages.size())
    return false;
  *buf = p->pages[(size_t)page].data();
  *len = (int)p->pages[(size_t)page].size();
  return true;
}

// Text of a resolved span, for readable assertions.
std::string SpanText(const Pages &pages, const ResolvedSpan &span) {
  std::string out;
  const std::vector<uint32_t> &buf = pages.pages[(size_t)span.page];
  for (int i = span.buf_begin; i < span.buf_end; i++)
    out.push_back((char)buf[(size_t)i]);
  return out;
}

void TestExtractSkipsTokens() {
  std::vector<uint32_t> buf;
  buf.push_back(TEXT_FONT_SIZE);
  buf.push_back(14);
  buf.push_back(TEXT_BOLD_ON);
  Append(&buf, "Hello");
  buf.push_back(TEXT_BOLD_OFF);
  buf.push_back(' ');
  buf.push_back(TEXT_LINK_START);
  buf.push_back(3);
  Append(&buf, "world");
  buf.push_back(TEXT_LINK_END);
  buf.push_back(TEXT_RTL_LINE_PX);
  buf.push_back(200);
  buf.push_back('\n');
  buf.push_back(TEXT_HR_BOUNDS);
  buf.push_back(4);
  buf.push_back(100);
  Append(&buf, "!");

  VisibleText text;
  annotation_text_utils::ExtractVisibleText(buf.data(), (int)buf.size(),
                                            &text);
  test::ExpectStrEq("visible text", VisibleUtf8(text).c_str(),
                    "Hello world\n\n!");
  test::ExpectEq("H maps past font-size token", text.buf_index[0], 3);
  test::ExpectEq("w maps past link token", text.buf_index[6], 12);
  test::ExpectEq("chars and map agree", (int)text.chars.size(),
                 (int)text.buf_index.size());
}

void TestNormalizeCollapsesWhitespace() {
  std::vector<uint32_t> buf = Buf("  quick\n\n brown");
  buf.push_back(0xAD); // soft hyphen
  Append(&buf, "fox");
  buf.push_back(0xA0); // nbsp
  Append(&buf, "!");
  VisibleText raw, norm;
  annotation_text_utils::ExtractVisibleText(buf.data(), (int)buf.size(), &raw);
  annotation_text_utils::NormalizeVisibleText(raw, &norm);
  test::ExpectStrEq("normalized", VisibleUtf8(norm).c_str(),
                    " quick brownfox !");
}

void TestUtf8RoundTrip() {
  const std::string s = "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80";
  const std::vector<uint32_t> cps = annotation_text_utils::Utf8ToCodepoints(s);
  test::ExpectEq("codepoint count", (int)cps.size(), 9);
  test::ExpectEqU("e acute", cps[3], 0xE9);
  test::ExpectEqU("emoji", cps[8], 0x1F600);
  test::ExpectStrEq("round trip",
                    annotation_text_utils::CodepointsToUtf8(cps, 0, cps.size())
                        .c_str(),
                    s.c_str());
}

void TestBuildAnchor() {
  std::vector<uint32_t> buf = Buf("It was the best of times,\nit was");
  std::string quote, prefix;
  // Select "best of times,\nit" (buffer indices 11..28).
  test::ExpectTrue("anchor built",
                   annotation_text_utils::BuildAnchorFromBufferRange(
                       buf.data(), (int)buf.size(), 11, 28, 100, 8, &quote,
                       &prefix));
  test::ExpectStrEq("quote normalized", quote.c_str(), "best of times, it");
  test::ExpectStrEq("prefix", prefix.c_str(), "was the");

  test::ExpectFalse("whitespace-only range",
                    annotation_text_utils::BuildAnchorFromBufferRange(
                        buf.data(), (int)buf.size(), 25, 26, 100, 8, &quote,
                        &prefix));

  test::ExpectTrue("capped quote",
                   annotation_text_utils::BuildAnchorFromBufferRange(
                       buf.data(), (int)buf.size(), 0, (int)buf.size(), 6, 8,
                       &quote, &prefix));
  test::ExpectStrEq("cap trims trailing space", quote.c_str(), "It was");
  test::ExpectStrEq("no prefix at page start", prefix.c_str(), "");
}

void TestRemapPageHint() {
  test::ExpectEq("same count", annotation_text_utils::RemapPageHint(5, 10, 10),
                 5);
  test::ExpectEq("halved", annotation_text_utils::RemapPageHint(8, 10, 5), 4);
  test::ExpectEq("clamped", annotation_text_utils::RemapPageHint(50, 10, 5), 4);
  test::ExpectEq("unknown count", annotation_text_utils::RemapPageHint(3, 0, 9),
                 3);
  test::ExpectEq("empty book", annotation_text_utils::RemapPageHint(3, 10, 0),
                 0);
}

void TestResolveAfterReflow() {
  Pages pages;
  pages.pages.push_back(Buf("Chapter one begins here."));
  pages.pages.push_back(Buf("Nothing to see."));
  pages.pages.push_back(Buf("The quick\nbrown fox jumps."));
  pages.pages.push_back(Buf("The end."));

  std::vector<ResolvedSpan> spans;
  // Created on page 5 of 8; the book now has 4 pages and wraps differently.
  test::ExpectTrue("resolved",
                   annotation_text_utils::ResolveAnchor(
                       "quick brown fox", "The", 5, 8, 4, PageBuffer, &pages,
                       3, &spans));
  test::ExpectEq("one span", (int)spans.size(), 1);
  test::ExpectEq("on page 2", spans[0].page, 2);
  test::ExpectStrEq("covers wrapped text", SpanText(pages, spans[0]).c_str(),
                    "quick\nbrown fox");
}

void TestPrefixDisambiguates() {
  Pages pages;
  pages.pages.push_back(Buf("He said yes. She said yes."));
  std::vector<ResolvedSpan> spans;
  test::ExpectTrue("resolved", annotation_text_utils::ResolveAnchor(
                                   "said yes", "She", 0, 1, 1, PageBuffer,
                                   &pages, 3, &spans));
  test::ExpectEq("second occurrence start", spans[0].buf_begin, 17);

  test::ExpectTrue("resolved without prefix",
                   annotation_text_utils::ResolveAnchor(
                       "said yes", "", 0, 1, 1, PageBuffer, &pages, 3,
                       &spans));
  test::ExpectEq("first occurrence start", spans[0].buf_begin, 3);
}

void TestNearestPageWinsTie() {
  Pages pages;
  for (int i = 0; i < 9; i++)
    pages.pages.push_back(Buf("filler text"));
  pages.pages[1] = Buf("a repeated line");
  pages.pages[6] = Buf("a repeated line");
  std::vector<ResolvedSpan> spans;
  test::ExpectTrue("resolved", annotation_text_utils::ResolveAnchor(
                                   "repeated line", "a", 5, 9, 9, PageBuffer,
                                   &pages, 3, &spans));
  test::ExpectEq("nearest page chosen", spans[0].page, 6);
}

void TestQuoteAcrossPageBreak() {
  Pages pages;
  pages.pages.push_back(Buf("the start of a long"));
  pages.pages.push_back(Buf("sentence that continues"));
  std::vector<ResolvedSpan> spans;
  test::ExpectTrue("resolved", annotation_text_utils::ResolveAnchor(
                                   "a long sentence that", "", 0, 1, 2,
                                   PageBuffer, &pages, 3, &spans));
  test::ExpectEq("two spans", (int)spans.size(), 2);
  test::ExpectStrEq("first part", SpanText(pages, spans[0]).c_str(), "a long");
  test::ExpectEq("second page", spans[1].page, 1);
  test::ExpectStrEq("second part", SpanText(pages, spans[1]).c_str(),
                    "sentence that");
}

void TestFallsBackToWholeBook() {
  Pages pages;
  for (int i = 0; i < 20; i++)
    pages.pages.push_back(Buf("nothing"));
  pages.pages[19] = Buf("the needle");
  std::vector<ResolvedSpan> spans;
  test::ExpectTrue("found outside window",
                   annotation_text_utils::ResolveAnchor(
                       "needle", "", 0, 20, 20, PageBuffer, &pages, 3,
                       &spans));
  test::ExpectEq("far page", spans[0].page, 19);
  test::ExpectFalse("missing quote",
                    annotation_text_utils::ResolveAnchor(
                        "haystack", "", 0, 20, 20, PageBuffer, &pages, 3,
                        &spans));
  test::ExpectTrue("no spans on failure", spans.empty());
  test::ExpectFalse("empty quote",
                    annotation_text_utils::ResolveAnchor(
                        "  ", "", 0, 20, 20, PageBuffer, &pages, 3, &spans));
}

} // namespace

int main() {
  TestExtractSkipsTokens();
  TestNormalizeCollapsesWhitespace();
  TestUtf8RoundTrip();
  TestBuildAnchor();
  TestRemapPageHint();
  TestResolveAfterReflow();
  TestPrefixDisambiguates();
  TestNearestPageWinsTie();
  TestQuoteAcrossPageBreak();
  TestFallsBackToWholeBook();
  return 0;
}
