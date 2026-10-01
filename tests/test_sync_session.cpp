#include "sync/sync_session.h"

#include "test_assert.h"

#include <string>

namespace {

// Two in-memory transports wired to each other.
class PipeTransport : public SyncTransport {
public:
  PipeTransport() : peer_(NULL), state_(kConnected) {}
  void Connect(PipeTransport *peer) { peer_ = peer; }
  void Poll(uint64_t) override {
    if (peer_ && state_ == kConnected && !outbox_.empty()) {
      peer_->inbox_ += outbox_;
      outbox_.clear();
    }
  }
  State GetState() const override { return state_; }
  void Send(const std::string &bytes) override { outbox_ += bytes; }
  bool SendQueueEmpty() const override { return outbox_.empty(); }
  std::string TakeReceived() override {
    std::string out;
    out.swap(inbox_);
    return out;
  }
  std::string Error() const override { return ""; }
  std::string PeerName() const override { return ""; }
  void Close() override { state_ = kClosed; }

  PipeTransport *peer_;
  State state_;
  std::string outbox_;
  std::string inbox_;
};

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

void RunUntilSettled(SyncSession *a, SyncSession *b) {
  for (uint64_t now = 1; now < 200; now++) {
    a->Poll(now);
    b->Poll(now);
    const bool a_settled = a->phase() == SyncSession::kDone ||
                           a->phase() == SyncSession::kFailed;
    const bool b_settled = b->phase() == SyncSession::kDone ||
                           b->phase() == SyncSession::kFailed;
    if (a_settled && b_settled)
      return;
  }
}

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

  PipeTransport ta, tb;
  ta.Connect(&tb);
  tb.Connect(&ta);
  SyncSession a("1234", 0xA, "Console A", ma, &ta);
  SyncSession b("1234", 0xB, "Console B", mb, &tb);
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
  test::ExpectStrEq("b keeps its newer position",
                    b.results()[0].merged.progress.quote.c_str(), "page B");
  test::ExpectEq("remote library visible", (int)a.remote_manifest().books.size(),
                 2);
}

void TestWrongPairingCodeFailsBothSides() {
  sync_manifest::Manifest ma, mb;
  ma.books.push_back(Book("secret.epub", 1));
  PipeTransport ta, tb;
  ta.Connect(&tb);
  tb.Connect(&ta);
  SyncSession a("1111", 0xA, "A", ma, &ta);
  SyncSession b("2222", 0xB, "B", mb, &tb);
  RunUntilSettled(&a, &b);
  test::ExpectTrue("a failed", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("b failed", b.phase() == SyncSession::kFailed);
  test::ExpectStrEq("a reason", a.error().c_str(), "Wrong pairing code");
  test::ExpectEq("library never sent", (int)b.remote_manifest().books.size(),
                 0);
}

void TestDisconnectFails() {
  sync_manifest::Manifest m;
  PipeTransport ta, tb;
  ta.Connect(&tb);
  tb.Connect(&ta);
  SyncSession a("1234", 0xA, "A", m, &ta);
  a.Poll(1);
  ta.state_ = SyncTransport::kClosed;
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

void TestCancel() {
  sync_manifest::Manifest m;
  PipeTransport ta, tb;
  ta.Connect(&tb);
  tb.Connect(&ta);
  SyncSession a("1234", 0xA, "A", m, &ta);
  SyncSession b("1234", 0xB, "B", m, &tb);
  a.Poll(1);
  a.Cancel();
  b.Poll(2);
  test::ExpectTrue("canceller stops", a.phase() == SyncSession::kFailed);
  test::ExpectTrue("other side told", b.phase() == SyncSession::kFailed);
  test::ExpectStrEq("other side reason", b.error().c_str(),
                    "The other 3DS cancelled");
}

} // namespace

int main() {
  TestBothSidesMergeSharedBooks();
  TestWrongPairingCodeFailsBothSides();
  TestDisconnectFails();
  TestTimeout();
  TestCancel();
  return 0;
}
