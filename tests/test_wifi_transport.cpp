// Real sockets over loopback: discovery, connection and a full sync session
// between a host and a joiner in one process.
#include "sync/sync_session.h"
#include "sync/wifi_transport.h"

#include "test_assert.h"

#include <string>
#include <unistd.h>

namespace {

WifiTransport::Options TestOptions(uint16_t base_port) {
  WifiTransport::Options options;
  options.discovery_port = base_port;
  options.stream_port = (uint16_t)(base_port + 1);
  return options;
}

void TestDiscoveryAndSync() {
  WifiTransport::Options host_options = TestOptions(47310);
  WifiTransport::Options join_options = TestOptions(47310);
  join_options.direct_host = "127.0.0.1";

  WifiTransport *host = WifiTransport::CreateHost("Host 3DS", host_options);
  WifiTransport *join = WifiTransport::CreateJoin("Join 3DS", join_options);
  test::ExpectTrue("host started", host->GetState() == SyncTransport::kWaiting);
  test::ExpectTrue("join started", join->GetState() == SyncTransport::kWaiting);

  // A library big enough to need many partial sends and receives.
  sync_manifest::Manifest mh, mj;
  for (int b = 0; b < 40; b++) {
    sync_manifest::BookEntry e;
    char name[32];
    snprintf(name, sizeof(name), "book-%02d.epub", b);
    e.file_name = name;
    e.file_size = 1000 + b;
    for (int h = 0; h < 50; h++) {
      Annotation a;
      a.id = ((uint64_t)0xAAu << 32) | (uint64_t)(b * 100 + h + 1);
      a.kind = Annotation::kHighlight;
      a.created = a.modified = 1000;
      a.quote = std::string(900, (char)('a' + h % 26));
      e.state.records.push_back(a);
    }
    mh.books.push_back(e);
    sync_manifest::BookEntry j = e;
    j.state.records.clear();
    mj.books.push_back(j);
  }

  SyncSession hs("4321", 0xAA, "Host 3DS", mh, host);
  SyncSession js("4321", 0xBB, "Join 3DS", mj, join);
  for (int i = 0; i < 5000; i++) {
    const uint64_t now = 1 + (uint64_t)i;
    hs.Poll(now);
    js.Poll(now);
    if ((hs.phase() == SyncSession::kDone ||
         hs.phase() == SyncSession::kFailed) &&
        (js.phase() == SyncSession::kDone ||
         js.phase() == SyncSession::kFailed))
      break;
    usleep(1000);
  }
  if (js.phase() == SyncSession::kFailed)
    fprintf(stderr, "join error: %s\n", js.error().c_str());
  if (hs.phase() == SyncSession::kFailed)
    fprintf(stderr, "host error: %s\n", hs.error().c_str());
  test::ExpectTrue("host done", hs.phase() == SyncSession::kDone);
  test::ExpectTrue("join done", js.phase() == SyncSession::kDone);
  test::ExpectStrEq("discovery carried the host name",
                    join->PeerName().c_str(), "Host 3DS");
  test::ExpectEq("all books matched", js.matched_books(), 40);
  test::ExpectEq("joiner received every book's highlights",
                 (int)js.results().size(), 40);
  test::ExpectEq("highlights per book",
                 (int)js.results()[0].merged.records.size(), 50);
  delete host;
  delete join;
}

void TestNoHostKeepsSearching() {
  WifiTransport::Options options = TestOptions(47320);
  options.direct_host = "127.0.0.1";
  WifiTransport *join = WifiTransport::CreateJoin("Lonely", options);
  for (int i = 0; i < 20; i++)
    join->Poll((uint64_t)i * 100);
  test::ExpectTrue("still searching", join->GetState() == SyncTransport::kWaiting);
  join->Close();
  test::ExpectTrue("closed", join->GetState() == SyncTransport::kClosed);
  delete join;
}

} // namespace

int main() {
  TestDiscoveryAndSync();
  TestNoHostKeepsSearching();
  return 0;
}
