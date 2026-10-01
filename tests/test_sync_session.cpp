#include "sync/sync_session.h"

#include "sync_test_helpers.h"
#include "test_assert.h"

#include <string>
#include <vector>

namespace {

Annotation Highlight(uint32_t console, uint32_t n, uint32_t modified,
                     const char *quote) {
  Annotation a;
  a.id = ((uint64_t)console << 32) | n;
  a.kind = Annotation::kHighlight;
  a.created = modified;
  a.modified = modified;
  a.quote = quote;
  return a;
}

sync_manifest::BookEntry Book(const char *name, uint64_t size) {
  sync_manifest::BookEntry e;
  e.file_name = name;
  e.file_size = size;
  return e;
}

std::string Content(size_t size, uint32_t seed) {
  std::string s(size, '\0');
  for (size_t i = 0; i < size; i++) {
    seed = seed * 1103515245u + 12345u;
    s[i] = (char)(seed >> 16);
  }
  return s;
}

bool Settled(const SyncSession &s) {
  return s.phase() == SyncSession::kDone || s.phase() == SyncSession::kFailed;
}

// Polls both sides; a side reaching kChoosing requests `want_*` (or skips).
void RunUntilSettled(SyncSession *a, SyncSession *b,
                     const std::vector<std::string> &want_a =
                         std::vector<std::string>(),
                     const std::vector<std::string> &want_b =
                         std::vector<std::string>(),
                     uint64_t step_ms = 1) {
  for (uint64_t now = 1; now < 100000; now += step_ms) {
    a->Poll(now);
    b->Poll(now);
    if (a->phase() == SyncSession::kChoosing)
      a->RequestBooks(want_a);
    if (b->phase() == SyncSession::kChoosing)
      b->RequestBooks(want_b);
    if (Settled(*a) && Settled(*b))
      return;
  }
}

struct Pair {
  PipeTransport ta, tb;
  Pair() {
    ta.Connect(&tb);
    tb.Connect(&ta);
  }
};

void TestBothSidesMergeSharedBooks() {
  sync_manifest::Manifest ma, mb;
  sync_manifest::BookEntry shared_a = Book("shared.epub", 1000);
  shared_a.state.records.push_back(Highlight(1, 1, 10, "from A"));
  shared_a.state.has_progress = true;
  shared_a.state.progress.last_read = 100;
  shared_a.state.progress.quote = "page A";
  ma.books.push_back(shared_a);
  ma.books.push_back(Book("only-on-a.epub", 5));

  sync_manifest::BookEntry shared_b = Book("shared.epub", 1000);
  shared_b.state.records.push_back(Highlight(2, 1, 11, "from B"));
  shared_b.state.has_progress = true;
  shared_b.state.progress.last_read = 200;
  shared_b.state.progress.quote = "page B";
  mb.books.push_back(shared_b);
  mb.books.push_back(Book("shared.epub", 999)); // different file, same name

  Pair p;
  SyncSession a("1234", 0xA, "Console A", ma, &p.ta);
  SyncSession b("1234", 0xB, "Console B", mb, &p.tb);
  RunUntilSettled(&a, &b);

  test::ExpectTrue("a done", a.phase() == SyncSession::kDone);
  test::ExpectTrue("b done", b.phase() == SyncSession::kDone);
  test::ExpectStrEq("a sees b's name", a.peer_name().c_str(), "Console B");
  test::ExpectEq("one shared book on a", a.matched_books(), 1);
  test::ExpectEq("a has a change", (int)a.results().size(), 1);
  test::ExpectEq("a gains b's highlight",
                 (int)a.results()[0].merged.records.size(), 2);
  test::ExpectStrEq("a takes b's newer position",
                    a.results()[0].merged.progress.quote.c_str(), "page B");
  test::ExpectEq("b gains a's highlight",
                 (int)b.results()[0].merged.records.size(), 2);
  test::ExpectEq("b is missing only-on-a", (int)b.MissingBooks().size(), 1);
  test::ExpectEq("a is missing b's other-size copy (different file)",
                 (int)a.MissingBooks().size(), 1);
}

void TestCopiesMissingBooksBothWays() {
  sync_manifest::Manifest ma, mb;
  const std::string novel = Content(150000, 1); // several chunks
  const std::string manga = Content(70000, 2);
  sync_manifest::BookEntry novel_entry = Book("novel.epub", novel.size());
  novel_entry.state.records.push_back(Highlight(1, 1, 10, "a quote"));
  ma.books.push_back(novel_entry);
  mb.books.push_back(Book("manga.cbz", manga.size()));

  MemorySource src_a, src_b;
  src_a.files[novel_entry.SyncId()] = novel;
  src_b.files[mb.books[0].SyncId()] = manga;
  MemorySink sink_a, sink_b;

  Pair p;
  SyncSession a("1234", 0xA, "A", ma, &p.ta, &src_a, &sink_a);
  SyncSession b("1234", 0xB, "B", mb, &p.tb, &src_b, &sink_b);
  std::vector<std::string> want_a(1, mb.books[0].SyncId());
  std::vector<std::string> want_b(1, novel_entry.SyncId());
  RunUntilSettled(&a, &b, want_a, want_b);

  test::ExpectTrue("a done", a.phase() == SyncSession::kDone);
  test::ExpectTrue("b done", b.phase() == SyncSession::kDone);
  test::ExpectTrue("b received the novel intact",
                   sink_b.complete["novel.epub"] == novel);
  test::ExpectTrue("a received the manga intact",
                   sink_a.complete["manga.cbz"] == manga);
  test::ExpectEq("b lists the book", (int)b.books_received().size(), 1);
  test::ExpectEq("a sent one book", a.books_sent(), 1);
  test::ExpectEq("new book's highlights come along",
                 (int)b.books_received()[0]->state.records.size(), 1);
  // File data is paced; frames of other types may sit on top of the limit.
  test::ExpectTrue("send queue stays bounded",
                   p.ta.max_queued_ <
                       SyncSession::kMaxQueuedBytes + 2 * SyncSession::kChunkBytes);
}

void TestResumesPartialCopy() {
  sync_manifest::Manifest ma, mb;
  const std::string book = Content(90000, 3);
  ma.books.push_back(Book("big.pdf", book.size()));
  MemorySource src;
  src.files[ma.books[0].SyncId()] = book;
  MemorySink sink;
  sink.partial["big.pdf"] = book.substr(0, 40000); // interrupted earlier

  Pair p;
  SyncSession a("1234", 0xA, "A", ma, &p.ta, &src, NULL);
  SyncSession b("1234", 0xB, "B", mb, &p.tb, NULL, &sink);
  RunUntilSettled(&a, &b, std::vector<std::string>(),
                  std::vector<std::string>(1, ma.books[0].SyncId()));
  test::ExpectTrue("done", b.phase() == SyncSession::kDone);
  test::ExpectTrue("resumed copy is intact", sink.complete["big.pdf"] == book);
}

void TestUnavailableAndRefusedBooks() {
  sync_manifest::Manifest ma, mb;
  ma.books.push_back(Book("listed-but-gone.epub", 10)); // no file to read
  ma.books.push_back(Book("too-big.cbz", 10));
  ma.books.push_back(Book("fine.txt", 3));
  MemorySource src;
  src.files[ma.books[2].SyncId()] = "abc";
  MemorySink sink;
  sink.refuse = "too-big.cbz";

  Pair p;
  SyncSession a("1234", 0xA, "A", ma, &p.ta, &src, NULL);
  SyncSession b("1234", 0xB, "B", mb, &p.tb, NULL, &sink);
  std::vector<std::string> want;
  for (size_t i = 0; i < ma.books.size(); i++)
    want.push_back(ma.books[i].SyncId());
  RunUntilSettled(&a, &b, std::vector<std::string>(), want);
  test::ExpectTrue("still completes", b.phase() == SyncSession::kDone);
  test::ExpectEq("one book arrived", (int)b.books_received().size(), 1);
  test::ExpectEq("two failures", (int)b.book_failures().size(), 2);
  test::ExpectTrue("refusal reason kept",
                   b.book_failures()[1].find("not enough space") !=
                       std::string::npos);
  test::ExpectTrue("fine copy intact", sink.complete["fine.txt"] == "abc");
}

void TestWriteFailureMovesOn() {
  sync_manifest::Manifest ma, mb;
  ma.books.push_back(Book("a.epub", 50000));
  ma.books.push_back(Book("b.epub", 4));
  MemorySource src;
  src.files[ma.books[0].SyncId()] = Content(50000, 9);
  src.files[ma.books[1].SyncId()] = "bbbb";
  MemorySink sink;
  sink.fail_writes = true;

  Pair p;
  SyncSession a("1234", 0xA, "A", ma, &p.ta, &src, NULL);
  SyncSession b("1234", 0xB, "B", mb, &p.tb, NULL, &sink);
  std::vector<std::string> want;
  want.push_back(ma.books[0].SyncId());
  want.push_back(ma.books[1].SyncId());
  RunUntilSettled(&a, &b, std::vector<std::string>(), want);
  test::ExpectTrue("completes despite write errors",
                   b.phase() == SyncSession::kDone);
  test::ExpectEq("both reported", (int)b.book_failures().size(), 2);
}

void TestKeepAliveWhileChoosing() {
  // B skips at once; A takes longer than the timeout to choose.
  sync_manifest::Manifest m;
  Pair p;
  SyncSession a("1234", 0xA, "A", m, &p.ta);
  SyncSession b("1234", 0xB, "B", m, &p.tb);
  uint64_t now = 1;
  for (; now < 50; now++) {
    a.Poll(now);
    b.Poll(now);
    if (b.phase() == SyncSession::kChoosing)
      b.SkipBooks();
  }
  test::ExpectTrue("a choosing", a.phase() == SyncSession::kChoosing);
  for (; now < 3 * SyncSession::kTimeoutMs; now += 500) {
    a.Poll(now);
    b.Poll(now);
  }
  test::ExpectTrue("b still waiting, not timed out",
                   b.phase() == SyncSession::kFinishing);
  a.SkipBooks();
  for (int i = 0; i < 10; i++, now++) {
    a.Poll(now);
    b.Poll(now);
  }
  test::ExpectTrue("a done", a.phase() == SyncSession::kDone);
  test::ExpectTrue("b done", b.phase() == SyncSession::kDone);
}

void TestWrongPairingCodeFailsBothSides() {
  sync_manifest::Manifest ma, mb;
  ma.books.push_back(Book("secret.epub", 1));
  Pair p;
  SyncSession a("1111", 0xA, "A", ma, &p.ta);
  SyncSession b("2222", 0xB, "B", mb, &p.tb);
  RunUntilSettled(&a, &b);
  test::ExpectTrue("a failed", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("b failed", b.phase() == SyncSession::kFailed);
  test::ExpectStrEq("a reason", a.error().c_str(), "Wrong pairing code");
  test::ExpectEq("library never sent", (int)b.remote_manifest().books.size(),
                 0);
}

void TestSameConsoleIdFailsBothSidesQuickly() {
  // e.g. the 3dslibris folder (with console_id.txt) copied between consoles.
  sync_manifest::Manifest m;
  Pair p;
  SyncSession a("1234", 0x77, "A", m, &p.ta);
  SyncSession b("1234", 0x77, "B", m, &p.tb);
  for (uint64_t now = 1; now < 6; now++) {
    a.Poll(now);
    b.Poll(now);
  }
  test::ExpectTrue("a failed", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("b failed (not left waiting)",
                   b.phase() == SyncSession::kFailed);
  test::ExpectTrue("same explanation on both sides", a.error() == b.error());
  test::ExpectTrue("explains the copied folder",
                   a.error().find("same 3dslibris ID") != std::string::npos);
}

void TestVersionMismatchExplained() {
  sync_manifest::Manifest m;
  Pair p;
  SyncSession a("1234", 0xA, "A", m, &p.ta);
  sync_protocol::Hello future;
  future.protocol_version = sync_protocol::kProtocolVersion + 1;
  future.pairing_code = "1234";
  future.console_id = 0xB;
  p.tb.Send(sync_protocol::EncodeFrame(sync_protocol::kHello,
                                       sync_protocol::EncodeHello(future)));
  p.tb.Poll(0);
  a.Poll(1);
  test::ExpectTrue("a failed", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("version message",
                   a.error().find("different 3dslibris versions") !=
                       std::string::npos);
  std::vector<sync_protocol::Frame> frames;
  sync_protocol::FrameDecoder dec;
  const std::string sent = p.tb.TakeReceived();
  dec.Feed(sent.data(), sent.size(), &frames);
  bool abort_seen = false;
  for (size_t i = 0; i < frames.size(); i++)
    abort_seen = abort_seen || frames[i].type == sync_protocol::kAbort;
  test::ExpectTrue("other side was told", abort_seen);
}

void TestDisconnectFails() {
  sync_manifest::Manifest m;
  Pair p;
  SyncSession a("1234", 0xA, "A", m, &p.ta);
  a.Poll(1);
  p.ta.state_ = SyncTransport::kClosed;
  a.Poll(2);
  test::ExpectTrue("disconnect fails", a.phase() == SyncSession::kFailed);
}

void TestTimeout() {
  sync_manifest::Manifest m;
  PipeTransport ta, silent;
  ta.Connect(&silent); // the other side never answers
  SyncSession a("1234", 0xA, "A", m, &ta);
  a.Poll(1);
  a.Poll(1 + SyncSession::kTimeoutMs + 1);
  test::ExpectTrue("times out", a.phase() == SyncSession::kFailed);
}

void TestCancelDuringCopyKeepsPartial() {
  sync_manifest::Manifest ma, mb;
  const std::string book = Content(400000, 5);
  ma.books.push_back(Book("long.cbz", book.size()));
  MemorySource src;
  src.files[ma.books[0].SyncId()] = book;
  MemorySink sink;
  Pair p;
  SyncSession a("1234", 0xA, "A", ma, &p.ta, &src, NULL);
  SyncSession b("1234", 0xB, "B", mb, &p.tb, NULL, &sink);
  std::vector<std::string> want(1, ma.books[0].SyncId());
  for (uint64_t now = 1; now < 1000; now++) {
    a.Poll(now);
    b.Poll(now);
    if (a.phase() == SyncSession::kChoosing)
      a.SkipBooks();
    if (b.phase() == SyncSession::kChoosing)
      b.RequestBooks(want);
    SyncSession::TransferProgress progress;
    if (b.GetTransferProgress(&progress) && progress.received > 100000) {
      b.Cancel();
      break;
    }
  }
  a.Poll(2000);
  test::ExpectTrue("b cancelled", b.phase() == SyncSession::kFailed);
  test::ExpectTrue("a told", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("partial kept for resume",
                   sink.partial["long.cbz"].size() > 100000 &&
                       book.compare(0, sink.partial["long.cbz"].size(),
                                    sink.partial["long.cbz"]) == 0);
}

} // namespace

int main() {
  TestBothSidesMergeSharedBooks();
  TestCopiesMissingBooksBothWays();
  TestResumesPartialCopy();
  TestUnavailableAndRefusedBooks();
  TestWriteFailureMovesOn();
  TestKeepAliveWhileChoosing();
  TestWrongPairingCodeFailsBothSides();
  TestSameConsoleIdFailsBothSidesQuickly();
  TestVersionMismatchExplained();
  TestDisconnectFails();
  TestTimeout();
  TestCancelDuringCopyKeepsPartial();
  return 0;
}
