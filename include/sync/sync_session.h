/*
    3dslibris - sync_session.h

    One sync between two consoles, independent of the connection type and of
    the app (host-tested over an in-memory pipe and over real sockets).

      both:   HELLO (protocol version, pairing code, console id, name)
      both:   check the other's HELLO; on a wrong code send ABORT and stop
      both:   MANIFEST (library + per-book state), sent only after a valid
              HELLO so a wrong-code device never sees the library
      both:   merge the other's manifest into the local books
      both:   DONE, then the session finishes once the other's DONE arrives

    The caller applies results() to its books when phase() is kDone.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "sync/sync_manifest.h"
#include "sync/sync_merge.h"
#include "sync/sync_protocol.h"
#include "sync/sync_transport.h"

class SyncSession {
public:
  enum Phase {
    kConnecting, // transport still waiting / searching
    kExchanging, // hello and manifests in flight
    kFinishing,  // merged; waiting for the other side's DONE
    kDone,
    kFailed,
  };

  struct BookResult {
    std::string sync_id;
    BookState merged;
    sync_merge::MergeStats stats;
  };

  static const uint64_t kTimeoutMs = 20000;

  SyncSession(const std::string &pairing_code, uint64_t console_id,
              const std::string &name, const sync_manifest::Manifest &local,
              SyncTransport *transport);

  void Poll(uint64_t now_ms);
  // Best effort: tells the other console, then stops.
  void Cancel();

  Phase phase() const { return phase_; }
  const std::string &error() const { return error_; }
  const std::string &peer_name() const { return peer_name_; }
  // Books both consoles have whose local state changed.
  const std::vector<BookResult> &results() const { return results_; }
  int matched_books() const { return matched_books_; }
  // The other console's library (valid once its manifest arrived).
  const sync_manifest::Manifest &remote_manifest() const { return remote_; }

private:
  void Fail(const std::string &message);
  // Fails and tells the other console why (ABORT with a reason code), so
  // both sides stop at once with the same explanation.
  void FailAndTell(const char *reason);
  void HandleFrame(const sync_protocol::Frame &frame);
  void MergeRemote();

  std::string pairing_code_;
  uint64_t console_id_;
  std::string name_;
  sync_manifest::Manifest local_;
  sync_manifest::Manifest remote_;
  SyncTransport *transport_;
  sync_protocol::FrameDecoder decoder_;
  Phase phase_;
  bool hello_sent_;
  bool hello_ok_;
  bool manifest_received_;
  bool done_sent_;
  bool done_received_;
  uint64_t last_progress_ms_;
  std::string error_;
  std::string peer_name_;
  std::vector<BookResult> results_;
  int matched_books_;
};
