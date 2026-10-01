// Library scan, file source and .part sink for book copies, on a temp dir.
#include "sync/sync_book_files.h"

#include "sync_test_helpers.h"
#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_root;

void WriteFile(const std::string &path, const std::string &data) {
  FILE *f = fopen(path.c_str(), "wb");
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
}

std::string ReadFile(const std::string &path) {
  std::string out;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    out.append(buf, n);
  fclose(f);
  return out;
}

bool Exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

bool IsBook(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (strcmp(dot, ".epub") == 0 || strcmp(dot, ".pdf") == 0);
}

uint64_t g_free = UINT64_MAX;
uint64_t FreeSpace(void *) { return g_free; }

sync_manifest::BookEntry Entry(const char *name, uint64_t size) {
  sync_manifest::BookEntry e;
  e.file_name = name;
  e.file_size = size;
  return e;
}

void TestScanLibrary() {
  const std::string lib = g_root + "/lib";
  const std::string other = g_root + "/romfs";
  mkdir(lib.c_str(), 0755);
  mkdir((lib + "/manga").c_str(), 0755);
  mkdir((lib + "/manga/deep").c_str(), 0755);
  mkdir((lib + "/.hidden").c_str(), 0755);
  mkdir(other.c_str(), 0755);
  WriteFile(lib + "/a.epub", "aaaa");
  WriteFile(lib + "/notes.txt.bak", "x");
  WriteFile(lib + "/b.epub.part", "partial");
  WriteFile(lib + "/manga/deep/c.pdf", "cc");
  WriteFile(lib + "/.hidden/d.epub", "d");
  WriteFile(other + "/a.epub", "aaaa"); // same file in a second root
  WriteFile(other + "/a2.epub", "z");

  std::vector<std::string> roots;
  roots.push_back(lib);
  roots.push_back(other);
  std::vector<sync_book_files::LocalBook> books =
      sync_book_files::ScanLibrary(roots, &IsBook);
  test::ExpectEq("books found", (int)books.size(), 3);
  bool deep = false, first_a = false;
  for (size_t i = 0; i < books.size(); i++) {
    if (books[i].file_name == "c.pdf")
      deep = books[i].folder == lib + "/manga/deep" && books[i].size == 2;
    if (books[i].file_name == "a.epub")
      first_a = books[i].folder == lib;
  }
  test::ExpectTrue("subfolders searched", deep);
  test::ExpectTrue("first root wins for the same file", first_a);
}

void TestSafeNames() {
  test::ExpectTrue("plain", sync_book_files::IsSafeFileName("My Book.epub"));
  test::ExpectTrue("unicode",
                   sync_book_files::IsSafeFileName("Anna\xE2\x80\x99s.epub"));
  test::ExpectTrue("folder", !sync_book_files::IsSafeFileName("../x.epub"));
  test::ExpectTrue("slash", !sync_book_files::IsSafeFileName("a/b.epub"));
  test::ExpectTrue("hidden", !sync_book_files::IsSafeFileName(".x.epub"));
  test::ExpectTrue("part", !sync_book_files::IsSafeFileName("x.epub.part"));
  test::ExpectTrue("colon", !sync_book_files::IsSafeFileName("sdmc:x.epub"));
  test::ExpectTrue("empty", !sync_book_files::IsSafeFileName(""));
}

void TestSourceReadsFromOffset() {
  const std::string dir = g_root + "/src";
  mkdir(dir.c_str(), 0755);
  WriteFile(dir + "/s.epub", "0123456789");
  std::vector<std::string> roots(1, dir);
  FileBookSource source(sync_book_files::ScanLibrary(roots, &IsBook));
  uint64_t total = 0;
  test::ExpectTrue("unknown id", !source.Open("nope#1", 0, &total));
  test::ExpectTrue("opens", source.Open("s.epub#10", 4, &total));
  test::ExpectEq("total", (int)total, 10);
  char buf[16];
  const size_t n = source.Read(buf, sizeof(buf));
  test::ExpectStrEq("rest of the file", std::string(buf, n).c_str(),
                    "456789");
  test::ExpectTrue("offset past the end refused",
                   !source.Open("s.epub#10", 11, &total));
}

void TestSinkCopiesResumesAndRefuses() {
  const std::string dir = g_root + "/dest";
  mkdir(dir.c_str(), 0755);
  FileBookSink sink(dir, &IsBook, &FreeSpace, NULL);
  uint64_t offset = 99;
  std::string error;

  // A fresh copy, interrupted, then resumed.
  test::ExpectTrue("begins", sink.Begin(Entry("n.epub", 8), &offset, &error));
  test::ExpectEq("from the start", (int)offset, 0);
  test::ExpectTrue("writes", sink.Write("abcd", 4));
  sink.Finish(false);
  test::ExpectTrue("partial kept", ReadFile(dir + "/n.epub.part") == "abcd");
  test::ExpectTrue("not shown as a book yet", !Exists(dir + "/n.epub"));
  test::ExpectTrue("resumes", sink.Begin(Entry("n.epub", 8), &offset, &error));
  test::ExpectEq("from the partial copy", (int)offset, 4);
  sink.Write("efgh", 4);
  test::ExpectTrue("finishes", sink.Finish(true));
  test::ExpectTrue("renamed into place", ReadFile(dir + "/n.epub") == "abcdefgh");
  test::ExpectTrue("part gone", !Exists(dir + "/n.epub.part"));

  // Same name, different file.
  test::ExpectTrue("name clash refused",
                   !sink.Begin(Entry("n.epub", 3), &offset, &error));
  test::ExpectTrue("clash reason",
                   error.find("already here") != std::string::npos);

  // Leftovers bigger than the book belong to some other file.
  WriteFile(dir + "/m.epub.part", "0123456789");
  test::ExpectTrue("begins over junk",
                   sink.Begin(Entry("m.epub", 4), &offset, &error));
  test::ExpectEq("starts over", (int)offset, 0);
  sink.Write("wxyz", 4);
  test::ExpectTrue("finishes", sink.Finish(true));
  test::ExpectTrue("junk replaced", ReadFile(dir + "/m.epub") == "wxyz");

  // Short file: rejected and removed.
  test::ExpectTrue("begins", sink.Begin(Entry("short.epub", 5), &offset,
                                        &error));
  sink.Write("ab", 2);
  test::ExpectTrue("size mismatch fails", !sink.Finish(true));
  test::ExpectTrue("damaged part removed", !Exists(dir + "/short.epub.part"));
  test::ExpectTrue("not added", !Exists(dir + "/short.epub"));

  // No space.
  g_free = 1000;
  error.clear();
  test::ExpectTrue("refused when full",
                   !sink.Begin(Entry("big.epub", 5000), &offset, &error));
  test::ExpectTrue("space reason", error.find("space") != std::string::npos);
  g_free = UINT64_MAX;

  // Unsafe or unsupported names.
  test::ExpectTrue("escape refused",
                   !sink.Begin(Entry("../evil.epub", 1), &offset, &error));
  test::ExpectTrue("not a book refused",
                   !sink.Begin(Entry("run.sh", 1), &offset, &error));
}

void TestSessionOverFiles() {
  // Two "consoles" with their own folders, one book copied A -> B.
  const std::string a_dir = g_root + "/console-a";
  const std::string b_dir = g_root + "/console-b";
  mkdir(a_dir.c_str(), 0755);
  mkdir(b_dir.c_str(), 0755);
  std::string data(300000, 'q');
  for (size_t i = 0; i < data.size(); i++)
    data[i] = (char)(i * 31 + (i >> 8));
  WriteFile(a_dir + "/novel.epub", data);

  std::vector<std::string> roots(1, a_dir);
  std::vector<sync_book_files::LocalBook> a_books =
      sync_book_files::ScanLibrary(roots, &IsBook);
  sync_manifest::Manifest ma, mb;
  ma.books.push_back(Entry("novel.epub", a_books[0].size));
  FileBookSource source(a_books);
  FileBookSink sink(b_dir, &IsBook);

  PipeTransport ta, tb;
  ta.Connect(&tb);
  tb.Connect(&ta);
  SyncSession a("1234", 0xA, "A", ma, &ta, &source, NULL);
  SyncSession b("1234", 0xB, "B", mb, &tb, NULL, &sink);
  for (uint64_t now = 1; now < 10000; now++) {
    a.Poll(now);
    b.Poll(now);
    if (a.phase() == SyncSession::kChoosing)
      a.SkipBooks();
    if (b.phase() == SyncSession::kChoosing)
      b.RequestBooks(std::vector<std::string>(1, a_books[0].SyncId()));
    if (a.phase() == SyncSession::kDone && b.phase() == SyncSession::kDone)
      break;
  }
  test::ExpectTrue("done", b.phase() == SyncSession::kDone);
  test::ExpectTrue("file copied", ReadFile(b_dir + "/novel.epub") == data);
}

} // namespace

int main() {
  const char *tmp = getenv("TMPDIR");
  std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp") +
                        "/3dslibris-syncfiles-XXXXXX";
  std::vector<char> buf(pattern.begin(), pattern.end());
  buf.push_back('\0');
  if (!mkdtemp(&buf[0])) {
    fprintf(stderr, "mkdtemp failed\n");
    return 1;
  }
  g_root = &buf[0];
  TestScanLibrary();
  TestSafeNames();
  TestSourceReadsFromOffset();
  TestSinkCopiesResumesAndRefuses();
  TestSessionOverFiles();
  std::string cmd = "rm -rf '" + g_root + "'";
  if (system(cmd.c_str()) != 0)
    fprintf(stderr, "cleanup failed\n");
  return 0;
}
