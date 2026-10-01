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
  a.color = 1;
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
      "\"author\":\"Author\",\"note\":\".green\\nmy note\","
      "\"category\":\"books\","
      "\"source_type\":\"3dslibris\",\"location\":12,"
      "\"location_type\":\"page\","
      "\"highlighted_at\":\"2026-10-01T12:34:56+00:00\"},"
      "{\"text\":\"Second\",\"title\":\"Book\",\"note\":\".yellow\","
      "\"category\":\"books\",\"source_type\":\"3dslibris\"}"
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

  // Uploaded by an older version (log only): unchanged ones still need
  // their color tag; edited ones their note too.
  std::vector<Highlight> all;
  all.push_back(Make(0x1234567800000001ULL, 100, "old, unchanged"));
  all.push_back(Make(0x1234567800000002ULL, 250, "old, note edited"));
  all.push_back(Make(0x1234567800000003ULL, 300, "new"));
  Highlight current = Make(0x1234567800000004ULL, 400, "up to date");
  current.readwise_uploaded = 400;
  all.push_back(current);
  Highlight changed = Make(0x1234567800000005ULL, 500, "edited since");
  changed.readwise_uploaded = 450;
  changed.readwise_id = 77;
  all.push_back(changed);
  const readwise_api_utils::Work work = readwise_api_utils::Classify(all, back);
  test::ExpectEq("create", (int)work.create.size(), 1);
  test::ExpectStrEq("new one", work.create[0].text.c_str(), "new");
  test::ExpectEq("update", (int)work.update.size(), 3);
  test::ExpectFalse("tag only", work.update_note[0]);
  test::ExpectTrue("old with edit", work.update_note[1]);
  test::ExpectStrEq("edited since", work.update[2].text.c_str(),
                    "edited since");
  test::ExpectTrue("edited note", work.update_note[2]);

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

void TestTagsAndUpdates() {
  test::ExpectStrEq("tag only", readwise_api_utils::NoteWithTag(0, "").c_str(),
                    ".yellow");
  test::ExpectStrEq("tag and note",
                    readwise_api_utils::NoteWithTag(4, "Big idea").c_str(),
                    ".purple\nBig idea");
  test::ExpectStrEq("patch",
                    readwise_api_utils::PatchNoteJson("a \"b\"").c_str(),
                    "{\"note\":\"a \\\"b\\\"\"}");
  test::ExpectStrEq("tag json", readwise_api_utils::TagJson("blue").c_str(),
                    "{\"name\":\"blue\"}");
  test::ExpectTrue("same text", readwise_api_utils::SameText(" x y\n", "x y"));
  test::ExpectFalse("different", readwise_api_utils::SameText("x y", "x z"));
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
  TestTagsAndUpdates();
  TestCleanToken();
  return 0;
}
