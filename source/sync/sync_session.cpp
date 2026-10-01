#include "sync/sync_session.h"

namespace {

// ABORT reason codes (sent between consoles) and what they mean.
static const char *kReasonPairing = "pairing code";
static const char *kReasonVersion = "version";
static const char *kReasonSameConsole = "same console";
static const char *kReasonDamaged = "damaged";
static const char *kReasonCancelled = "cancelled";

std::string ReasonMessage(const std::string &reason) {
  if (reason == kReasonPairing)
    return "Wrong pairing code";
  if (reason == kReasonVersion)
    return "The two consoles run different 3dslibris versions";
  if (reason == kReasonSameConsole)
    return "Both consoles report the same 3dslibris ID (was the 3dslibris "
           "folder copied between them?). Update both to this version";
  if (reason == kReasonDamaged)
    return "Data was damaged on the way. Try again";
  if (reason == kReasonCancelled)
    return "The other 3DS cancelled";
  return "The other 3DS stopped the sync (" + reason + ")";
}

} // namespace

SyncSession::SyncSession(const std::string &pairing_code, uint64_t console_id,
                         const std::string &name,
                         const sync_manifest::Manifest &local,
                         SyncTransport *transport, BookFileSource *source,
                         BookFileSink *sink)
    : pairing_code_(pairing_code), console_id_(console_id), name_(name),
      local_(local), transport_(transport), source_(source), sink_(sink),
      phase_(kConnecting), hello_sent_(false), hello_ok_(false),
      manifest_received_(false), done_sent_(false), done_received_(false),
      last_progress_ms_(0), last_send_ms_(0), matched_books_(0),
      request_index_(0), request_active_(false), request_received_(0),
      request_total_(0), serving_(false), serving_offset_(0),
      serving_total_(0), books_sent_(0) {}

void SyncSession::Send(uint8_t type, const std::string &payload) {
  transport_->Send(sync_protocol::EncodeFrame(type, payload));
}

void SyncSession::Fail(const std::string &message) {
  if (phase_ == kFailed || phase_ == kDone)
    return;
  if (request_active_ && sink_)
    sink_->Finish(false); // keep the partial copy for a later resume
  request_active_ = false;
  if (serving_ && source_)
    source_->Close();
  serving_ = false;
  error_ = message;
  phase_ = kFailed;
}

void SyncSession::FailAndTell(const char *reason) {
  if (phase_ == kFailed || phase_ == kDone)
    return;
  if (transport_ && transport_->GetState() == SyncTransport::kConnected) {
    // Anything already queued (e.g. our HELLO) goes first, then the reason.
    Send(sync_protocol::kAbort, reason);
    transport_->Poll(0);
  }
  Fail(std::string(reason) == kReasonCancelled ? std::string("Cancelled")
                                               : ReasonMessage(reason));
}

void SyncSession::Cancel() { FailAndTell(kReasonCancelled); }

std::vector<const sync_manifest::BookEntry *>
SyncSession::MissingBooks() const {
  std::vector<const sync_manifest::BookEntry *> missing;
  for (size_t i = 0; i < remote_.books.size(); i++)
    if (!local_.Find(remote_.books[i].SyncId()))
      missing.push_back(&remote_.books[i]);
  return missing;
}

void SyncSession::RequestBooks(const std::vector<std::string> &sync_ids) {
  if (phase_ != kChoosing)
    return;
  requests_ = sync_ids;
  request_index_ = 0;
  phase_ = kTransferring;
  StartNextRequest();
}

bool SyncSession::GetTransferProgress(TransferProgress *out) const {
  if (!out || phase_ != kTransferring || request_index_ >= requests_.size())
    return false;
  const sync_manifest::BookEntry *book = remote_.Find(requests_[request_index_]);
  out->file_name = book ? book->file_name : requests_[request_index_];
  out->received = request_received_;
  out->total = request_total_;
  out->index = (int)request_index_ + 1;
  out->count = (int)requests_.size();
  return true;
}

void SyncSession::StartNextRequest() {
  while (request_index_ < requests_.size()) {
    const sync_manifest::BookEntry *book =
        remote_.Find(requests_[request_index_]);
    std::string error;
    uint64_t offset = 0;
    if (!book) {
      book_failures_.push_back(requests_[request_index_] + ": not offered");
    } else if (!sink_) {
      book_failures_.push_back(book->file_name + ": can't save here");
    } else if (!sink_->Begin(*book, &offset, &error)) {
      book_failures_.push_back(book->file_name + ": " + error);
    } else if (offset >= book->file_size) {
      // Already here in full (an earlier copy finished but wasn't renamed).
      if (sink_->Finish(true))
        books_received_.push_back(book);
      else
        book_failures_.push_back(book->file_name + ": couldn't save it");
    } else {
      request_active_ = true;
      request_received_ = offset;
      request_total_ = book->file_size;
      Send(sync_protocol::kBookRequest,
           sync_protocol::EncodeBookRequest(book->SyncId(), offset));
      return;
    }
    request_index_++;
  }
  // Nothing (more) to fetch.
  if (!done_sent_) {
    Send(sync_protocol::kDone, "");
    done_sent_ = true;
  }
  phase_ = kFinishing;
}

void SyncSession::FinishCurrentRequest(bool complete,
                                       const std::string &failure) {
  const sync_manifest::BookEntry *book =
      remote_.Find(requests_[request_index_]);
  std::string reason = failure;
  if (sink_ && !sink_->Finish(complete) && complete) {
    complete = false;
    reason = "couldn't save it";
  }
  request_active_ = false;
  if (complete && book)
    books_received_.push_back(book);
  else if (book)
    book_failures_.push_back(book->file_name + ": " + reason);
  request_index_++;
  StartNextRequest();
}

void SyncSession::PumpServing() {
  while (true) {
    if (!serving_) {
      if (serve_queue_.empty())
        return;
      const std::pair<std::string, uint64_t> req = serve_queue_.front();
      serve_queue_.pop_front();
      uint64_t total = 0;
      // Only books we offered in our manifest are served.
      if (!source_ || !local_.Find(req.first) ||
          !source_->Open(req.first, req.second, &total) ||
          req.second > total) {
        if (source_)
          source_->Close();
        Send(sync_protocol::kFileEnd,
             sync_protocol::EncodeFileEnd(req.first,
                                          sync_protocol::kFileUnavailable));
        continue;
      }
      serving_ = true;
      serving_id_ = req.first;
      serving_offset_ = req.second;
      serving_total_ = total;
    }
    while (serving_offset_ < serving_total_) {
      if (transport_->QueuedBytes() >= kMaxQueuedBytes)
        return; // the connection is busy; continue next poll
      std::string data(kChunkBytes, '\0');
      const size_t n = source_->Read(&data[0], data.size());
      if (n == 0)
        break; // file shrank or read error: end here
      data.resize(n);
      Send(sync_protocol::kFileChunk,
           sync_protocol::EncodeFileChunk(serving_id_, serving_offset_,
                                          serving_total_, data));
      serving_offset_ += n;
    }
    const bool complete = serving_offset_ >= serving_total_;
    source_->Close();
    serving_ = false;
    Send(sync_protocol::kFileEnd,
         sync_protocol::EncodeFileEnd(
             serving_id_, complete ? sync_protocol::kFileComplete
                                   : sync_protocol::kFileUnavailable));
    if (complete)
      books_sent_++;
  }
}

void SyncSession::Poll(uint64_t now_ms) {
  if (!transport_ || phase_ == kDone || phase_ == kFailed)
    return;
  transport_->Poll(now_ms);
  if (last_progress_ms_ == 0)
    last_progress_ms_ = now_ms;

  const SyncTransport::State state = transport_->GetState();
  if (state == SyncTransport::kFailed) {
    Fail(transport_->Error().empty() ? "Connection lost"
                                     : transport_->Error());
    return;
  }

  if (phase_ == kConnecting) {
    if (state != SyncTransport::kConnected) {
      last_progress_ms_ = now_ms; // no timeout while waiting for the peer
      return;
    }
    phase_ = kExchanging;
    last_progress_ms_ = now_ms;
  }

  if (!hello_sent_) {
    sync_protocol::Hello hello;
    hello.pairing_code = pairing_code_;
    hello.console_id = console_id_;
    hello.name = name_;
    Send(sync_protocol::kHello, sync_protocol::EncodeHello(hello));
    hello_sent_ = true;
    last_send_ms_ = now_ms;
  }

  const std::string bytes = transport_->TakeReceived();
  if (!bytes.empty()) {
    last_progress_ms_ = now_ms;
    std::vector<sync_protocol::Frame> frames;
    if (decoder_.Feed(bytes.data(), bytes.size(), &frames) !=
        sync_protocol::FrameDecoder::kOk) {
      FailAndTell(kReasonDamaged);
      return;
    }
    for (size_t i = 0; i < frames.size() && phase_ != kFailed; i++)
      HandleFrame(frames[i]);
    if (phase_ == kFailed)
      return;
  }

  PumpServing();

  // Keep the connection alive while one side is choosing books.
  if (!transport_->SendQueueEmpty())
    last_send_ms_ = now_ms;
  else if (manifest_received_ && now_ms - last_send_ms_ >= kPingIntervalMs) {
    Send(sync_protocol::kPing, "");
    last_send_ms_ = now_ms;
  }

  // Flush what was just queued.
  transport_->Poll(now_ms);

  if (done_sent_ && done_received_ && !serving_ && serve_queue_.empty() &&
      transport_->SendQueueEmpty()) {
    phase_ = kDone;
    return;
  }
  if (state == SyncTransport::kClosed) {
    Fail("The other 3DS disconnected");
    return;
  }
  if (now_ms - last_progress_ms_ > kTimeoutMs)
    Fail("The other 3DS stopped responding");
}

void SyncSession::HandleFrame(const sync_protocol::Frame &frame) {
  switch (frame.type) {
  case sync_protocol::kHello: {
    sync_protocol::Hello hello;
    if (!sync_protocol::DecodeHello(frame.payload, &hello)) {
      FailAndTell(kReasonDamaged);
      return;
    }
    if (hello.protocol_version != sync_protocol::kProtocolVersion) {
      FailAndTell(kReasonVersion);
      return;
    }
    if (hello.pairing_code != pairing_code_) {
      FailAndTell(kReasonPairing);
      return;
    }
    if (hello.console_id == console_id_) {
      FailAndTell(kReasonSameConsole);
      return;
    }
    peer_name_ = hello.name;
    hello_ok_ = true;
    Send(sync_protocol::kManifest, sync_manifest::Serialize(local_));
    return;
  }
  case sync_protocol::kManifest:
    if (!hello_ok_) {
      FailAndTell(kReasonPairing);
      return;
    }
    if (!sync_manifest::Parse(frame.payload, &remote_)) {
      FailAndTell(kReasonDamaged);
      return;
    }
    manifest_received_ = true;
    MergeRemote();
    phase_ = kChoosing;
    return;
  case sync_protocol::kBookRequest: {
    std::string id;
    uint64_t offset = 0;
    if (!hello_ok_ ||
        !sync_protocol::DecodeBookRequest(frame.payload, &id, &offset)) {
      FailAndTell(kReasonDamaged);
      return;
    }
    serve_queue_.push_back(std::make_pair(id, offset));
    return;
  }
  case sync_protocol::kFileChunk: {
    std::string id, data;
    uint64_t offset = 0, total = 0;
    if (!sync_protocol::DecodeFileChunk(frame.payload, &id, &offset, &total,
                                        &data)) {
      FailAndTell(kReasonDamaged);
      return;
    }
    if (request_index_ >= requests_.size() || id != requests_[request_index_])
      return; // leftover chunk of a book we gave up on
    if (!request_active_)
      return; // writing failed earlier; wait for its FILE_END
    if (offset != request_received_ || total != request_total_) {
      FailAndTell(kReasonDamaged);
      return;
    }
    if (!sink_->Write(data.data(), data.size())) {
      // Keep reading (and ignoring) this book's remaining chunks until its
      // FILE_END, then move on.
      const sync_manifest::BookEntry *book = remote_.Find(id);
      book_failures_.push_back((book ? book->file_name : id) +
                               ": couldn't write to the SD card");
      sink_->Finish(false);
      request_active_ = false;
      return;
    }
    request_received_ += data.size();
    return;
  }
  case sync_protocol::kFileEnd: {
    std::string id;
    sync_protocol::FileEndStatus status;
    if (!sync_protocol::DecodeFileEnd(frame.payload, &id, &status)) {
      FailAndTell(kReasonDamaged);
      return;
    }
    if (request_index_ >= requests_.size() || id != requests_[request_index_])
      return; // stale end of a book we already moved past
    if (!request_active_) {
      // Writing failed earlier (already recorded); move on.
      request_index_++;
      StartNextRequest();
      return;
    }
    const bool complete = status == sync_protocol::kFileComplete &&
                          request_received_ == request_total_;
    FinishCurrentRequest(complete, complete ? "" : "not available there");
    return;
  }
  case sync_protocol::kDone:
    done_received_ = true;
    return;
  case sync_protocol::kAbort:
    Fail(ReasonMessage(frame.payload));
    return;
  case sync_protocol::kPing:
  default:
    return;
  }
}

void SyncSession::MergeRemote() {
  results_.clear();
  matched_books_ = 0;
  for (size_t i = 0; i < local_.books.size(); i++) {
    const sync_manifest::BookEntry &mine = local_.books[i];
    const sync_manifest::BookEntry *theirs = remote_.Find(mine.SyncId());
    if (!theirs)
      continue;
    matched_books_++;
    BookResult result;
    result.sync_id = mine.SyncId();
    result.merged = sync_merge::Merge(mine.state, theirs->state, &result.stats);
    if (result.stats.Changed())
      results_.push_back(result);
  }
}
