#include "book/annotation_store_utils.h"

#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

Annotation Make(uint32_t id, const char *quote, const char *prefix,
                const char *note) {
  Annotation a;
  a.id = id;
  a.created = 1700000000u + id;
  a.page_hint = (uint16_t)(id * 10);
  a.page_count_hint = 500;
  a.quote = quote;
  a.prefix = prefix;
  a.note = note;
  return a;
}

std::string TempPath(const char *name) {
  const char *dir = getenv("TMPDIR");
  std::string path = (dir && *dir) ? dir : "/tmp";
  path += "/";
  path += name;
  return path;
}

void TestEscapeRoundTrip() {
  const std::string tricky = "tab\there\nnew\\line\r\\n literal";
  const std::string escaped = annotation_store_utils::EscapeField(tricky);
  test::ExpectTrue("no raw tab", escaped.find('\t') == std::string::npos);
  test::ExpectTrue("no raw newline", escaped.find('\n') == std::string::npos);
  test::ExpectStrEq("round trip",
                    annotation_store_utils::UnescapeField(escaped).c_str(),
                    tricky.c_str());
}

void TestSerializeParseRoundTrip() {
  std::vector<Annotation> in;
  in.push_back(Make(1, "caf\xC3\xA9 <b>&amp;</b> \"quoted\"", "before",
                    "my note\nsecond line\twith tab"));
  in.push_back(Make(7, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "", ""));

  std::vector<Annotation> out;
  test::ExpectTrue("parsed",
                   annotation_store_utils::Parse(
                       annotation_store_utils::Serialize(in), &out));
  test::ExpectEq("count", (int)out.size(), 2);
  test::ExpectEqU("id", out[0].id, 1);
  test::ExpectEqU("created", out[0].created, 1700000001u);
  test::ExpectEq("page hint", out[0].page_hint, 10);
  test::ExpectEq("page count hint", out[0].page_count_hint, 500);
  test::ExpectStrEq("quote", out[0].quote.c_str(), in[0].quote.c_str());
  test::ExpectStrEq("prefix", out[0].prefix.c_str(), "before");
  test::ExpectStrEq("note", out[0].note.c_str(), in[0].note.c_str());
  test::ExpectStrEq("cjk quote", out[1].quote.c_str(), in[1].quote.c_str());
  test::ExpectStrEq("empty note", out[1].note.c_str(), "");
}

void TestParseRejectsAndSkips() {
  std::vector<Annotation> out;
  test::ExpectFalse("wrong header",
                    annotation_store_utils::Parse("hello\n", &out));
  test::ExpectFalse("empty data", annotation_store_utils::Parse("", &out));

  const std::string data =
      "3DSLIBRIS-ANNOTATIONS 1\r\n"
      "1\t0\t3\t10\tgood one\t\t\r\n"
      "not\ta\tvalid\tline\n"
      "0\t0\t3\t10\tzero id\t\t\n"
      "2\t0\t99999\t10\tpage overflow\t\t\n"
      "3\t0\t3\t10\t\t\tempty quote\n"
      "4\t0\t4\t10\tlast one\tpre\tnote";
  test::ExpectTrue("parsed", annotation_store_utils::Parse(data, &out));
  test::ExpectEq("kept valid lines", (int)out.size(), 2);
  test::ExpectStrEq("crlf stripped", out[0].quote.c_str(), "good one");
  test::ExpectStrEq("no trailing newline", out[1].note.c_str(), "note");
}

void TestBuildFileName() {
  const std::string a =
      annotation_store_utils::BuildFileName("sdmc:/books", "My Book: 1.epub");
  const std::string b =
      annotation_store_utils::BuildFileName("sdmc:/other", "My Book: 1.epub");
  test::ExpectStrEq("stable",
                    annotation_store_utils::BuildFileName("sdmc:/books",
                                                          "My Book: 1.epub")
                        .c_str(),
                    a.c_str());
  test::ExpectTrue("folder matters", a != b);
  test::ExpectTrue("readable part", a.find("My_Book__1.epub_") == 0);
  test::ExpectTrue("no unsafe chars", a.find_first_of(" :/\\") ==
                                          std::string::npos);
  const std::string long_name(200, 'x');
  test::ExpectTrue("bounded length",
                   annotation_store_utils::BuildFileName("f", long_name)
                           .size() < 60);
}

void TestFileRoundTrip() {
  const std::string path = TempPath("3dslibris_annotations_test.txt");
  remove(path.c_str());
  remove((path + ".tmp").c_str());

  std::vector<Annotation> out;
  out.push_back(Make(9, "stale", "", ""));
  test::ExpectTrue("missing file is empty",
                   annotation_store_utils::LoadFile(path, &out));
  test::ExpectTrue("missing file clears list", out.empty());

  std::vector<Annotation> in;
  in.push_back(Make(3, "saved quote", "p", "n"));
  test::ExpectTrue("saved", annotation_store_utils::SaveFile(path, in));
  test::ExpectTrue("saved again", annotation_store_utils::SaveFile(path, in));
  test::ExpectTrue("loaded", annotation_store_utils::LoadFile(path, &out));
  test::ExpectEq("loaded count", (int)out.size(), 1);
  test::ExpectStrEq("loaded quote", out[0].quote.c_str(), "saved quote");

  // Simulate a crash after the old file was removed but before the rename.
  test::ExpectEq("rename to tmp",
                 rename(path.c_str(), (path + ".tmp").c_str()), 0);
  test::ExpectTrue("tmp fallback", annotation_store_utils::LoadFile(path, &out));
  test::ExpectEq("tmp fallback count", (int)out.size(), 1);

  test::ExpectTrue("empty save", annotation_store_utils::SaveFile(
                                     path, std::vector<Annotation>()));
  FILE *fp = fopen(path.c_str(), "rb");
  test::ExpectTrue("empty save removes file", fp == NULL);
  if (fp)
    fclose(fp);
  fp = fopen((path + ".tmp").c_str(), "rb");
  test::ExpectTrue("empty save removes tmp", fp == NULL);
  if (fp)
    fclose(fp);
}

void TestNextId() {
  std::vector<Annotation> list;
  test::ExpectEqU("first id", annotation_store_utils::NextId(list), 1);
  list.push_back(Make(4, "a", "", ""));
  list.push_back(Make(2, "b", "", ""));
  test::ExpectEqU("after max", annotation_store_utils::NextId(list), 5);
}

} // namespace

int main() {
  TestEscapeRoundTrip();
  TestSerializeParseRoundTrip();
  TestParseRejectsAndSkips();
  TestBuildFileName();
  TestFileRoundTrip();
  TestNextId();
  return 0;
}
