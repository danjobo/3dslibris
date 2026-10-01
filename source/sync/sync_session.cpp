#include "sync/sync_session.h"

SyncSession::SyncSession(const std::string &pairing_code, uint64_t console_id,
                         const std::string &name,
                         const sync_manifest::Manifest &local,
                         SyncTransport *transport)
    : pairing_code_(pairing_code), console_id_(console_id), name_(name),
      local_(local), transport_(transport), phase_(kConnecting),
      hello_sent_(false), hello_ok_(false), manifest_received_(false),
      done_sent_(false), done_received_(false), last_progress_ms_(0),
      matched_books_(0) {}

void SyncSession::Fail(const std::string &message) {
  if (phase_ == kFailed || phase_ == kDone)
    return;
  error_ = message;
  phase_ = kFailed;
}

void SyncSession::Cancel() {
  if (phase_ == kDone || phase_ == kFailed)
    return;
  if (transport_ && transport_->GetState() == SyncTransport::kConnected) {
    transport_->Send(
        sync_protocol::EncodeFrame(sync_protocol::kAbort, "cancelled"));
    transport_->Poll(0);
  }
  Fail("Cancelled");
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
    transport_->Send(sync_protocol::EncodeFrame(
        sync_protocol::kHello, sync_protocol::EncodeHello(hello)));
    hello_sent_ = true;
  }

  const std::string bytes = transport_->TakeReceived();
  if (!bytes.empty()) {
    last_progress_ms_ = now_ms;
    std::vector<sync_protocol::Frame> frames;
    if (decoder_.Feed(bytes.data(), bytes.size(), &frames) !=
        sync_protocol::FrameDecoder::kOk) {
      Fail("The other 3DS sent damaged data");
      return;
    }
    for (size_t i = 0; i < frames.size() && phase_ != kFailed; i++)
      HandleFrame(frames[i]);
    if (phase_ == kFailed)
      return;
  }

  if (manifest_received_ && !done_sent_) {
    transport_->Send(sync_protocol::EncodeFrame(sync_protocol::kDone, ""));
    done_sent_ = true;
    phase_ = kFinishing;
  }

  // Flush what was just queued.
  transport_->Poll(now_ms);

  if (done_sent_ && done_received_ && transport_->SendQueueEmpty()) {
    phase_ = kDone;
    return;
  }
  if (state == SyncTransport::kClosed && !(done_sent_ && done_received_)) {
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
      Fail("The other 3DS sent damaged data");
      return;
    }
    if (hello.protocol_version != sync_protocol::kProtocolVersion) {
      Fail("The other 3DS runs a different 3dslibris version");
      return;
    }
    if (hello.pairing_code != pairing_code_) {
      transport_->Send(
          sync_protocol::EncodeFrame(sync_protocol::kAbort, "pairing code"));
      transport_->Poll(0); // let the other console know why
      Fail("Wrong pairing code");
      return;
    }
    if (hello.console_id == console_id_) {
      Fail("Can't sync a 3DS with itself");
      return;
    }
    peer_name_ = hello.name;
    hello_ok_ = true;
    transport_->Send(sync_protocol::EncodeFrame(
        sync_protocol::kManifest, sync_manifest::Serialize(local_)));
    return;
  }
  case sync_protocol::kManifest:
    if (!hello_ok_) {
      Fail("The other 3DS skipped pairing");
      return;
    }
    if (!sync_manifest::Parse(frame.payload, &remote_)) {
      Fail("The other 3DS sent damaged data");
      return;
    }
    manifest_received_ = true;
    MergeRemote();
    return;
  case sync_protocol::kDone:
    done_received_ = true;
    return;
  case sync_protocol::kAbort:
    Fail(frame.payload == "pairing code" ? "Wrong pairing code"
                                         : "The other 3DS cancelled");
    return;
  default:
    // Book transfer messages and future types: ignored by this phase.
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
