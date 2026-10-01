#include "book/readwise_api_utils.h"

#include "test_assert.h"

#include <string>
#include <vector>

using readwise_api_utils::Highlight;

namespace {

Highlight Make(uint64_t id, uint32_t modified, const char *text) {
  Highlight h;
  h.id = id;
  h.modified = modified;
  h.text = text;
  h.title = "Book";
  return h;
}

void TestJsonString() {
  test::ExpectStrEq("plain", readwise_api_utils::JsonString("abc").c_str(),
                    "\"abc\"");
  test::ExpectStrEq(
      "escapes",
      readwise_api_utils::JsonString("say \"hi\"\\\n\tx").c_str(),
      "\"say \\\"hi\\\"\\\\\\n\\tx\"");
  test::ExpectStrEq("control",
                    readwise_api_utils::JsonString(std::string("a\x01")).c_str(),
                    "\"a\\u0001\"");
  test::ExpectStrEq("utf-8 kept",
                    readwise_api_utils::JsonString("caf\xC3\xA9 \xE2\x80\x99")
                        .c_str(),
                    "\"caf\xC3\xA9 \xE2\x80\x99\"");
}

void TestIsoTime() {
  test::ExpectStrEq("epoch", readwise_api_utils::IsoTime(0).c_str(),
                    "1970-01-01T00:00:00+00:00");
  // 2026-10-01 12:34:56 UTC
  test::ExpectStrEq("date", readwise_api_utils::IsoTime(1790858096u).c_str(),
                    "2026-10-01T12:34:56+00:00");
  // Leap day.
  test::ExpectStrEq("leap day", readwise_api_utils::IsoTime(1709164800u).c_str(),
                    "2024-02-29T00:00:00+00:00");
}

void TestBuildJson() {
  std::vector<Highlight> hs;
  Highlight a = Make(1, 10, "First \"quote\"");
  a.author = "Author";
  a.note = "my note";
  a.location = 12;
  a.highlighted_at = 1790858096u;
  hs.push_back(a);
  Highlight b = Make(2, 11, "Second");
  hs.push_back(b);
  const std::string json = readwise_api_utils::BuildHighlightsJson(hs);
  test::ExpectStrEq(
      "body", json.c_str(),
      "{\"highlights\":["
      "{\"text\":\"First \\\"quote\\\"\",\"title\":\"Book\","
      "\"author\":\"Author\",\"note\":\"my note\",\"category\":\"books\","
      "\"source_type\":\"3dslibris\",\"location\":12,"
      "\"location_type\":\"page\","
      "\"highlighted_at\":\"2026-10-01T12:34:56+00:00\"},"
      "{\"text\":\"Second\",\"title\":\"Book\",\"category\":\"books\","
      "\"source_type\":\"3dslibris\"}"
      "]}");
  test::ExpectStrEq("empty",
                    readwise_api_utils::BuildHighlightsJson(
                        std::vector<Highlight>())
                        .c_str(),
                    "{\"highlights\":[]}");
}

void TestTruncatesLongFields() {
  std::vector<Highlight> hs;
  Highlight h = Make(1, 1, "x");
  h.title.clear();
  // 600 two-byte characters: cut at 511 characters, never mid-character.
  for (int i = 0; i < 600; i++)
    h.title += "\xC3\xA9";
  hs.push_back(h);
  const std::string json = readwise_api_utils::BuildHighlightsJson(hs);
  const size_t start = json.find("\"title\":\"") + 9;
  const size_t end = json.find('"', start);
  test::ExpectEq("title bytes", (int)(end - start), 511 * 2);
}

void TestLogRoundTripAndPending() {
  readwise_api_utils::UploadLog log;
  log[0x1234567800000001ULL] = 100;
  log[0x1234567800000002ULL] = 200;
  const std::string text = readwise_api_utils::SerializeLog(log);
  const readwise_api_utils::UploadLog back = readwise_api_utils::ParseLog(text);
  test::ExpectEq("entries", (int)back.size(), 2);
  test::ExpectEq("value", (int)back.find(0x1234567800000002ULL)->second, 200);

  std::vector<Highlight> all;
  all.push_back(Make(0x1234567800000001ULL, 100, "unchanged"));
  all.push_back(Make(0x1234567800000002ULL, 250, "note edited"));
  all.push_back(Make(0x1234567800000003ULL, 300, "new"));
  const std::vector<Highlight> pending =
      readwise_api_utils::Pending(all, back);
  test::ExpectEq("pending", (int)pending.size(), 2);
  test::ExpectStrEq("edited", pending[0].text.c_str(), "note edited");
  test::ExpectStrEq("new", pending[1].text.c_str(), "new");

  test::ExpectEq("junk log is empty",
                 (int)readwise_api_utils::ParseLog("nonsense\n1\t2\n").size(),
                 0);
  test::ExpectEq("bad lines skipped",
                 (int)readwise_api_utils::ParseLog(
                     "3DSLIBRIS-READWISE 1\nzz\t1\n00000000000000aa\tx\n"
                     "00000000000000bb\t5\r\n")
                     .size(),
                 1);
}

void TestCleanToken() {
  test::ExpectStrEq("trimmed",
                    readwise_api_utils::CleanToken(" abc123XYZ \r\n").c_str(),
                    "abc123XYZ");
  test::ExpectStrEq("inner spaces dropped",
                    readwise_api_utils::CleanToken("ab c\td").c_str(), "abcd");
}

} // namespace

int main() {
  TestJsonString();
  TestIsoTime();
  TestBuildJson();
  TestTruncatesLongFields();
  TestLogRoundTripAndPending();
  TestCleanToken();
  return 0;
}
