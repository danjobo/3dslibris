#include "book/annotation_store_utils.h"

#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

const uint32_t kConsoleA = 0x1234ABCDu;
const uint32_t kConsoleB = 0x0BADF00Du;

Annotation MakeHighlight(uint64_t id, const char *quote, const char *prefix,
                         const char *note) {
  Annotation a;
  a.id = id;
  a.kind = Annotation::kHighlight;
  a.created = 1700000000u + (uint32_t)(id & 0xFF);
  a.modified = a.created + 5;
  a.page_hint = (uint16_t)((id & 0xFF) * 10);
  a.page_count_hint = 500;
  a.quote = quote;
  a.prefix = prefix;
  a.note = note;
  return a;
}

uint64_t IdOf(uint32_t console, uint32_t counter) {
  return ((uint64_t)console << 32) | counter;
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
  BookState in;
  in.records.push_back(MakeHighlight(IdOf(kConsoleA, 1),
                                     "caf\xC3\xA9 \"quoted\"", "before",
                                     "my note\nsecond line\twith tab"));
  Annotation bookmark;
  bookmark.id = IdOf(kConsoleB, 7);
  bookmark.kind = Annotation::kBookmark;
  bookmark.created = 1700000100u;
  bookmark.modified = 1700000200u;
  bookmark.page_hint = 12;
  bookmark.page_count_hint = 300;
  bookmark.quote = "Chapter Two";
  in.records.push_back(bookmark);
  Annotation tombstone = MakeHighlight(IdOf(kConsoleA, 2), "", "", "");
  tombstone.deleted = true;
  in.records.push_back(tombstone);
  in.has_progress = true;
  in.progress.last_read = 1700000300u;
  in.progress.page_hint = 42;
  in.progress.page_count_hint = 300;
  in.progress.quote = "\xE6\x97\xA5\xE6\x9C\xAC start of page";
  in.progress.prefix = "";

  BookState out;
  test::ExpectTrue("parsed",
                   annotation_store_utils::Parse(
                       annotation_store_utils::Serialize(in), kConsoleA,
                       &out));
  test::ExpectEq("record count", (int)out.records.size(), 3);
  test::ExpectTrue("highlight id",
                   out.records[0].id == IdOf(kConsoleA, 1));
  test::ExpectTrue("highlight kind",
                   out.records[0].kind == Annotation::kHighlight);
  test::ExpectEqU("modified", out.records[0].modified,
                  in.records[0].modified);
  test::ExpectStrEq("quote", out.records[0].quote.c_str(),
                    in.records[0].quote.c_str());
  test::ExpectStrEq("note", out.records[0].note.c_str(),
                    in.records[0].note.c_str());
  test::ExpectTrue("bookmark kind",
                   out.records[1].kind == Annotation::kBookmark);
  test::ExpectTrue("bookmark id from other console",
                   out.records[1].id == IdOf(kConsoleB, 7));
  test::ExpectEq("bookmark page", out.records[1].page_hint, 12);
  test::ExpectTrue("tombstone kept", out.records[2].deleted);
  test::ExpectTrue("progress present", out.has_progress);
  test::ExpectEqU("progress last read", out.progress.last_read, 1700000300u);
  test::ExpectEq("progress page", out.progress.page_hint, 42);
  test::ExpectStrEq("progress quote", out.progress.quote.c_str(),
                    in.progress.quote.c_str());
}

void TestParseV1MigratesIds() {
  const std::string data =
      "3DSLIBRIS-ANNOTATIONS 1\r\n"
      "1\t1700000000\t3\t10\tgood one\t\tnote\r\n"
      "not\ta\tvalid\tline\n"
      "0\t0\t3\t10\tzero id\t\t\n"
      "2\t0\t99999\t10\tpage overflow\t\t\n"
      "3\t0\t3\t10\t\t\tempty quote\n"
      "4\t1700000009\t4\t10\tlast one\tpre\tnote";
  BookState out;
  test::ExpectTrue("v1 parsed",
                   annotation_store_utils::Parse(data, kConsoleA, &out));
  test::ExpectEq("kept valid lines", (int)out.records.size(), 2);
  test::ExpectTrue("v1 id moved into console space",
                   out.records[0].id == IdOf(kConsoleA, 1));
  test::ExpectTrue("v1 is highlight",
                   out.records[0].kind == Annotation::kHighlight);
  test::ExpectEqU("v1 modified = created", out.records[1].modified,
                  1700000009u);
  test::ExpectFalse("v1 has no progress", out.has_progress);
  test::ExpectStrEq("crlf stripped", out.records[0].note.c_str(), "note");
}

void TestParseRejects() {
  BookState out;
  test::ExpectFalse("wrong header",
                    annotation_store_utils::Parse("hello\n", kConsoleA, &out));
  test::ExpectFalse("empty data",
                    annotation_store_utils::Parse("", kConsoleA, &out));
  const std::string data =
      "3DSLIBRIS-BOOKSTATE 2\n"
      "H\tzz\t1\t1\t0\t1\t1\tbad id\t\t\n"
      "H\t0000000000000000\t1\t1\t0\t1\t1\tzero id\t\t\n"
      "H\t0000000100000001\t1\t1\t0\t1\t1\t\t\t\n"
      "X\t0000000100000002\t1\t1\t0\t1\t1\tunknown kind\t\t\n"
      "P\tbad\t1\t1\tq\tp\n"
      "B\t0000000100000003\t1\t1\t0\t5\t9\t\t\t\n";
  test::ExpectTrue("v2 parsed",
                   annotation_store_utils::Parse(data, kConsoleA, &out));
  test::ExpectEq("only the textless bookmark survives",
                 (int)out.records.size(), 1);
  test::ExpectTrue("fixed-layout bookmark without text",
                   out.records[0].kind == Annotation::kBookmark);
  test::ExpectFalse("bad progress ignored", out.has_progress);
}

void TestColors() {
  BookState state;
  Annotation green = MakeHighlight(IdOf(kConsoleA, 1), "green one", "", "");
  green.color = 1;
  Annotation purple = MakeHighlight(IdOf(kConsoleA, 2), "purple one", "", "n");
  purple.color = 4;
  state.records.push_back(green);
  state.records.push_back(purple);
  const std::string text = annotation_store_utils::Serialize(state);
  test::ExpectTrue("written as v3",
                   text.compare(0, 21, "3DSLIBRIS-BOOKSTATE 3") == 0);
  BookState out;
  test::ExpectTrue("parsed",
                   annotation_store_utils::Parse(text, kConsoleA, &out));
  test::ExpectEq("count", (int)out.records.size(), 2);
  test::ExpectEq("green kept", (int)out.records[0].color, 1);
  test::ExpectEq("purple kept", (int)out.records[1].color, 4);
  test::ExpectStrEq("note still read", out.records[1].note.c_str(), "n");

  // v2 files (before colors) load as yellow.
  const std::string v2 = "3DSLIBRIS-BOOKSTATE 2\n"
                         "H\t0000000100000001\t1\t1\t0\t1\t1\told\t\tnote\n";
  test::ExpectTrue("v2 parsed",
                   annotation_store_utils::Parse(v2, kConsoleA, &out));
  test::ExpectEq("v2 record", (int)out.records.size(), 1);
  test::ExpectEq("v2 is yellow", (int)out.records[0].color, 0);
  test::ExpectStrEq("v2 note", out.records[0].note.c_str(), "note");

  // A color this version doesn't know shows as yellow; junk is rejected.
  const std::string future =
      "3DSLIBRIS-BOOKSTATE 3\n"
      "H\t0000000100000001\t1\t1\t0\t1\t1\tnew\t\t\t9\n"
      "H\t0000000100000002\t1\t1\t0\t1\t1\tbad\t\t\tblue\n";
  test::ExpectTrue("future parsed",
                   annotation_store_utils::Parse(future, kConsoleA, &out));
  test::ExpectEq("bad color line skipped", (int)out.records.size(), 1);
  test::ExpectEq("unknown color is yellow", (int)out.records[0].color, 0);
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

  BookState out;
  out.records.push_back(MakeHighlight(IdOf(kConsoleA, 9), "stale", "", ""));
  test::ExpectTrue("missing file is empty",
                   annotation_store_utils::LoadFile(path, kConsoleA, &out));
  test::ExpectTrue("missing file clears state", out.Empty());

  BookState in;
  in.records.push_back(MakeHighlight(IdOf(kConsoleA, 3), "saved quote", "p",
                                     "n"));
  test::ExpectTrue("saved", annotation_store_utils::SaveFile(path, in));
  test::ExpectTrue("saved again", annotation_store_utils::SaveFile(path, in));
  test::ExpectTrue("loaded",
                   annotation_store_utils::LoadFile(path, kConsoleA, &out));
  test::ExpectEq("loaded count", (int)out.records.size(), 1);
  test::ExpectStrEq("loaded quote", out.records[0].quote.c_str(),
                    "saved quote");

  // Simulate a crash after the old file was removed but before the rename.
  test::ExpectEq("rename to tmp",
                 rename(path.c_str(), (path + ".tmp").c_str()), 0);
  test::ExpectTrue("tmp fallback",
                   annotation_store_utils::LoadFile(path, kConsoleA, &out));
  test::ExpectEq("tmp fallback count", (int)out.records.size(), 1);

  test::ExpectTrue("empty save",
                   annotation_store_utils::SaveFile(path, BookState()));
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
  BookState state;
  test::ExpectTrue("first id",
                   annotation_store_utils::NextId(state, kConsoleA) ==
                       IdOf(kConsoleA, 1));
  state.records.push_back(MakeHighlight(IdOf(kConsoleA, 4), "a", "", ""));
  state.records.push_back(MakeHighlight(IdOf(kConsoleB, 90), "b", "", ""));
  test::ExpectTrue("after own max, ignoring other console",
                   annotation_store_utils::NextId(state, kConsoleA) ==
                       IdOf(kConsoleA, 5));
}

} // namespace

int main() {
  TestEscapeRoundTrip();
  TestSerializeParseRoundTrip();
  TestParseV1MigratesIds();
  TestParseRejects();
  TestColors();
  TestBuildFileName();
  TestFileRoundTrip();
  TestNextId();
  return 0;
}
