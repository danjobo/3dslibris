// Reliable byte stream over a simulated lossy datagram channel (as local
// wireless behaves), and a full sync session with a book copy over it.
#include "sync/reliable_link.h"
#include "sync/sync_session.h"

#include "sync_test_helpers.h"
#include "test_assert.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct Rng {
  uint32_t state;
  explicit Rng(uint32_t seed) : state(seed) {}
  uint32_t Next() {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
  }
  bool Chance(int percent) { return (int)(Next() % 100) < percent; }
};

// One direction of the air: datagrams arrive after a delay, maybe lost,
// duplicated or reordered.
struct Channel {
  struct InFlight {
    uint64_t arrive_ms;
    std::string datagram;
  };
  Rng rng;
  int loss_percent;
  int dup_percent;
  uint32_t jitter_ms; // > frame time reorders packets
  bool blackout;
  std::vector<InFlight> air;
  size_t max_datagram;

  explicit Channel(uint32_t seed)
      : rng(seed), loss_percent(0), dup_percent(0), jitter_ms(20),
        blackout(false),
        max_datagram(0) {}

  void Send(const std::string &d, uint64_t now) {
    max_datagram = std::max(max_datagram, d.size());
    if (blackout || rng.Chance(loss_percent))
      return;
    InFlight f;
    f.arrive_ms = now + 5 + (jitter_ms ? rng.Next() % jitter_ms : 0);
    f.datagram = d;
    air.push_back(f);
    if (rng.Chance(dup_percent))
      air.push_back(f);
  }

  void Deliver(uint64_t now, ReliableLink *to) {
    std::vector<InFlight> later;
    for (size_t i = 0; i < air.size(); i++) {
      if (air[i].arrive_ms <= now)
        to->OnDatagram(air[i].datagram.data(), air[i].datagram.size());
      else
        later.push_back(air[i]);
    }
    air.swap(later);
  }
};

void Step(ReliableLink *a, ReliableLink *b, Channel *ab, Channel *ba,
          uint64_t now) {
  std::vector<std::string> out;
  a->CollectOutgoing(now, &out);
  for (size_t i = 0; i < out.size(); i++)
    ab->Send(out[i], now);
  out.clear();
  b->CollectOutgoing(now, &out);
  for (size_t i = 0; i < out.size(); i++)
    ba->Send(out[i], now);
  ab->Deliver(now, b);
  ba->Deliver(now, a);
}

std::string Data(size_t size, uint32_t seed) {
  Rng rng(seed);
  std::string s(size, '\0');
  for (size_t i = 0; i < size; i++)
    s[i] = (char)rng.Next();
  return s;
}

void TestBothWaysOverBadChannel(int loss, int dup) {
  ReliableLink a, b;
  Channel ab(1 + loss), ba(2 + dup);
  ab.loss_percent = ba.loss_percent = loss;
  ab.dup_percent = ba.dup_percent = dup;
  if (loss == 0 && dup == 0)
    ab.jitter_ms = ba.jitter_ms = 0; // clean and in order
  const std::string to_b = Data(300000, 7), to_a = Data(120000, 8);
  // Written in uneven pieces, as session frames are.
  for (size_t off = 0; off < to_b.size(); off += 7001)
    a.Queue(to_b.substr(off, 7001));
  b.Queue(to_a);
  std::string got_b, got_a;
  uint64_t now = 0;
  for (; now < 600000; now += 16) {
    Step(&a, &b, &ab, &ba, now);
    got_b += b.TakeReceived();
    got_a += a.TakeReceived();
    if (got_b.size() == to_b.size() && got_a.size() == to_a.size() &&
        a.AllAcknowledged() && b.AllAcknowledged())
      break;
  }
  char label[64];
  snprintf(label, sizeof(label), "a->b intact (loss %d%%, dup %d%%)", loss,
           dup);
  test::ExpectTrue(label, got_b == to_b);
  snprintf(label, sizeof(label), "b->a intact (loss %d%%, dup %d%%)", loss,
           dup);
  test::ExpectTrue(label, got_a == to_a);
  test::ExpectTrue("everything acknowledged",
                   a.AllAcknowledged() && b.AllAcknowledged());
  test::ExpectTrue("datagrams fit a local wireless frame",
                   ab.max_datagram <= 0x5C6 && ba.max_datagram <= 0x5C6);
  if (loss == 0 && dup == 0)
    test::ExpectEq("no resends on a clean channel", (int)a.resent_packets(),
                   0);
}

void TestWindowLimitsPacketsInFlight() {
  ReliableLink a;
  a.Queue(std::string(100000, 'x'));
  std::vector<std::string> out;
  a.CollectOutgoing(0, &out);
  test::ExpectEq("window", (int)out.size(), (int)ReliableLink::kWindow);
  out.clear();
  a.CollectOutgoing(10, &out); // nothing acknowledged, not timed out yet
  test::ExpectEq("waits for acks", (int)out.size(), 0);
  a.CollectOutgoing(ReliableLink::kRetransmitMs, &out);
  test::ExpectEq("resends the window", (int)out.size(),
                 (int)ReliableLink::kWindow);
}

void TestRecoversAfterBlackout() {
  ReliableLink a, b;
  Channel ab(3), ba(4);
  const std::string data = Data(50000, 9);
  a.Queue(data);
  std::string got;
  uint64_t now = 0;
  ab.blackout = ba.blackout = true; // e.g. walked out of range
  for (; now < 5000; now += 16) {
    Step(&a, &b, &ab, &ba, now);
    got += b.TakeReceived();
  }
  test::ExpectTrue("nothing arrived", got.empty());
  ab.blackout = ba.blackout = false;
  for (; now < 60000 && !(got.size() == data.size() && a.AllAcknowledged());
       now += 16) {
    Step(&a, &b, &ab, &ba, now);
    got += b.TakeReceived();
  }
  test::ExpectTrue("all arrived after the blackout", got == data);
}

void TestIgnoresJunk() {
  ReliableLink a;
  a.OnDatagram("", 0);
  a.OnDatagram("hello world, not ours", 21);
  const char bad_type[10] = {'L', 9, 0, 0, 0, 0, 0, 0, 0, 0};
  a.OnDatagram(bad_type, sizeof(bad_type));
  test::ExpectTrue("no data from junk", a.TakeReceived().empty());
  std::vector<std::string> out;
  a.CollectOutgoing(0, &out);
  test::ExpectEq("no ack for junk", (int)out.size(), 0);
}

// A SyncTransport over a ReliableLink and a lossy Channel, like the local
// wireless transport.
class LinkTransport : public SyncTransport {
public:
  LinkTransport(Channel *out) : out_(out), now_(0) {}
  void Poll(uint64_t now_ms) override {
    now_ = now_ms;
    std::vector<std::string> datagrams;
    link_.CollectOutgoing(now_ms, &datagrams);
    for (size_t i = 0; i < datagrams.size(); i++)
      out_->Send(datagrams[i], now_ms);
  }
  State GetState() const override { return kConnected; }
  void Send(const std::string &bytes) override { link_.Queue(bytes); }
  bool SendQueueEmpty() const override { return link_.AllAcknowledged(); }
  size_t QueuedBytes() const override { return link_.QueuedBytes(); }
  std::string TakeReceived() override { return link_.TakeReceived(); }
  std::string Error() const override { return ""; }
  std::string PeerName() const override { return ""; }
  void Close() override {}
  ReliableLink link_;

private:
  Channel *out_;
  uint64_t now_;
};

void TestSessionCopiesBookOverLossyLink() {
  Channel ab(11), ba(12);
  ab.loss_percent = ba.loss_percent = 10;
  LinkTransport ta(&ab), tb(&ba);

  sync_manifest::Manifest ma, mb;
  const std::string book = Data(400000, 13);
  sync_manifest::BookEntry e;
  e.file_name = "travel.epub";
  e.file_size = book.size();
  ma.books.push_back(e);
  MemorySource source;
  source.files[e.SyncId()] = book;
  MemorySink sink;

  SyncSession a("1234", 0xA, "A", ma, &ta, &source, NULL);
  SyncSession b("1234", 0xB, "B", mb, &tb, NULL, &sink);
  for (uint64_t now = 1; now < 600000; now += 16) {
    a.Poll(now);
    b.Poll(now);
    ab.Deliver(now, &tb.link_);
    ba.Deliver(now, &ta.link_);
    if (a.phase() == SyncSession::kChoosing)
      a.SkipBooks();
    if (b.phase() == SyncSession::kChoosing)
      b.RequestBooks(std::vector<std::string>(1, e.SyncId()));
    if ((a.phase() == SyncSession::kDone ||
         a.phase() == SyncSession::kFailed) &&
        (b.phase() == SyncSession::kDone || b.phase() == SyncSession::kFailed))
      break;
  }
  if (b.phase() == SyncSession::kFailed)
    fprintf(stderr, "b error: %s\n", b.error().c_str());
  test::ExpectTrue("a done", a.phase() == SyncSession::kDone);
  test::ExpectTrue("b done", b.phase() == SyncSession::kDone);
  test::ExpectTrue("book intact over a 10% lossy link",
                   sink.complete["travel.epub"] == book);
}

} // namespace

int main() {
  TestBothWaysOverBadChannel(0, 0);
  TestBothWaysOverBadChannel(5, 0);
  TestBothWaysOverBadChannel(0, 3); // reordering and duplicates only
  TestBothWaysOverBadChannel(25, 10);
  TestWindowLimitsPacketsInFlight();
  TestRecoversAfterBlackout();
  TestIgnoresJunk();
  TestSessionCopiesBookOverLossyLink();
  return 0;
}
