/*
    3dslibris - sync_session.h

    One sync between two consoles, independent of the connection type and of
    the app (host-tested over an in-memory pipe and over real sockets).

      both:   HELLO (protocol version, pairing code, console id, name)
      both:   check the other's HELLO; on any problem send ABORT (with a
              reason) and stop
      both:   MANIFEST (library + per-book state), sent only after a valid
              HELLO so a wrong-code device never sees the library
      both:   merge the other's manifest into the local books -> kChoosing
      local:  RequestBooks() copies books only the other console has, one at
              a time (BOOK_REQUEST, FILE_CHUNK..., FILE_END), resuming
              partial copies; SkipBooks() copies nothing
      both:   meanwhile, answer the other console's BOOK_REQUESTs
      both:   DONE once its own copying is finished; the session finishes
              when both sent DONE and nothing is still being sent

    The caller applies results() (merged state) when phase() reaches
    kChoosing or later, and reads books_received() when kDone.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

#include "sync/sync_manifest.h"
#include "sync/sync_merge.h"
#include "sync/sync_protocol.h"
#include "sync/sync_transport.h"

// Reads local book files for the other console.
class BookFileSource {
public:
  virtual ~BookFileSource() {}
  // Opens the book with this sync id at offset; total is its size.
  virtual bool Open(const std::string &sync_id, uint64_t offset,
                    uint64_t *total) = 0;
  // Returns bytes read (0 at end of file or on error).
  virtual size_t Read(char *buf, size_t max) = 0;
  virtual void Close() = 0;
};

// Writes books received from the other console.
class BookFileSink {
public:
  virtual ~BookFileSink() {}
  // Prepares to receive book; *resume_offset is how much of it is already
  // here from an interrupted copy. False (with *error) skips the book, e.g.
  // when the SD card is too full.
  virtual bool Begin(const sync_manifest::BookEntry &book,
                     uint64_t *resume_offset, std::string *error) = 0;
  virtual bool Write(const char *data, size_t len) = 0;
  // complete: the whole file arrived; keep it. Otherwise keep the partial
  // copy for a later resume.
  virtual bool Finish(bool complete) = 0;
};

class SyncSession {
public:
  enum Phase {
    kConnecting,   // transport still waiting / searching
    kExchanging,   // hello and manifests in flight
    kChoosing,     // merged; waiting for RequestBooks() / SkipBooks()
    kTransferring, // copying requested books
    kFinishing,    // own copying done; waiting for the other side
    kDone,
    kFailed,
  };

  struct BookResult {
    std::string sync_id;
    BookState merged;
    sync_merge::MergeStats stats;
  };

  struct TransferProgress {
    std::string file_name;
    uint64_t received;
    uint64_t total;
    int index;   // 1-based position in the requested list
    int count;   // books requested
  };

  static const uint64_t kTimeoutMs = 30000;
  static const uint64_t kPingIntervalMs = 5000;
  static const size_t kChunkBytes = 16 * 1024;
  // Keep at most this much file data queued on the connection.
  static const size_t kMaxQueuedBytes = 64 * 1024;

  SyncSession(const std::string &pairing_code, uint64_t console_id,
              const std::string &name, const sync_manifest::Manifest &local,
              SyncTransport *transport, BookFileSource *source = NULL,
              BookFileSink *sink = NULL);

  void Poll(uint64_t now_ms);
  // Best effort: tells the other console, then stops.
  void Cancel();

  // In kChoosing: copy these books (sync ids from MissingBooks()).
  void RequestBooks(const std::vector<std::string> &sync_ids);
  // In kChoosing: copy nothing.
  void SkipBooks() { RequestBooks(std::vector<std::string>()); }

  Phase phase() const { return phase_; }
  const std::string &error() const { return error_; }
  const std::string &peer_name() const { return peer_name_; }
  // Books both consoles have whose local state changed.
  const std::vector<BookResult> &results() const { return results_; }
  int matched_books() const { return matched_books_; }
  // The other console's library (valid once its manifest arrived).
  const sync_manifest::Manifest &remote_manifest() const { return remote_; }
  // Books only the other console has.
  std::vector<const sync_manifest::BookEntry *> MissingBooks() const;
  bool GetTransferProgress(TransferProgress *out) const;
  // Books fully copied here, and ones that couldn't be (with a reason).
  const std::vector<const sync_manifest::BookEntry *> &books_received() const {
    return books_received_;
  }
  const std::vector<std::string> &book_failures() const {
    return book_failures_;
  }
  int books_sent() const { return books_sent_; }

private:
  void Fail(const std::string &message);
  void FailAndTell(const char *reason);
  void HandleFrame(const sync_protocol::Frame &frame);
  void MergeRemote();
  void StartNextRequest();
  void FinishCurrentRequest(bool complete, const std::string &failure);
  void PumpServing();
  void Send(uint8_t type, const std::string &payload);

  std::string pairing_code_;
  uint64_t console_id_;
  std::string name_;
  sync_manifest::Manifest local_;
  sync_manifest::Manifest remote_;
  SyncTransport *transport_;
  BookFileSource *source_;
  BookFileSink *sink_;
  sync_protocol::FrameDecoder decoder_;
  Phase phase_;
  bool hello_sent_;
  bool hello_ok_;
  bool manifest_received_;
  bool done_sent_;
  bool done_received_;
  uint64_t last_progress_ms_;
  uint64_t last_send_ms_;
  std::string error_;
  std::string peer_name_;
  std::vector<BookResult> results_;
  int matched_books_;

  // Our requests.
  std::vector<std::string> requests_;
  size_t request_index_;
  bool request_active_;
  uint64_t request_received_;
  uint64_t request_total_;
  std::vector<const sync_manifest::BookEntry *> books_received_;
  std::vector<std::string> book_failures_;

  // Serving the other console's requests.
  std::deque<std::pair<std::string, uint64_t> > serve_queue_;
  bool serving_;
  std::string serving_id_;
  uint64_t serving_offset_;
  uint64_t serving_total_;
  int books_sent_;
};
