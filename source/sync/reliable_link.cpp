/*
    3dslibris - reliable_link.cpp

    See include/sync/reliable_link.h.
*/

#include "sync/reliable_link.h"

namespace {

const char kMagic = 'L';
const uint8_t kTypeData = 1;
const uint8_t kTypeAck = 2;
// Resend a hole once this many later packets arrived.
const uint32_t kFastResendGap = 3;

void PutU32(std::string *out, uint32_t v) {
  for (int i = 0; i < 4; i++)
    out->push_back((char)((v >> (8 * i)) & 0xFF));
}

uint32_t GetU32(const char *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++)
    v |= (uint32_t)(unsigned char)p[i] << (8 * i);
  return v;
}

// a comes before b, allowing for wrap-around.
bool SeqBefore(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

} // namespace

ReliableLink::ReliableLink()
    : pending_offset_(0), in_flight_bytes_(0), next_seq_(0), highest_acked_(0),
      any_acked_(false), expected_seq_(0), ack_due_(false),
      resent_packets_(0) {}

void ReliableLink::Queue(const std::string &bytes) { pending_ += bytes; }

size_t ReliableLink::QueuedBytes() const {
  return (pending_.size() - pending_offset_) + in_flight_bytes_;
}

uint32_t ReliableLink::ReceivedBitmap() const {
  // Bit i: packet expected_seq_ + 1 + i is here.
  uint32_t bits = 0;
  for (std::map<uint32_t, std::string>::const_iterator it =
           out_of_order_.begin();
       it != out_of_order_.end(); ++it) {
    const uint32_t offset = it->first - expected_seq_ - 1;
    if (offset < 32)
      bits |= 1u << offset;
  }
  return bits;
}

std::string ReliableLink::Encode(uint8_t type, uint32_t seq,
                                 const std::string &payload) const {
  std::string out;
  out.reserve(kHeaderBytes + payload.size());
  out.push_back(kMagic);
  out.push_back((char)type);
  PutU32(&out, seq);
  PutU32(&out, expected_seq_);
  PutU32(&out, ReceivedBitmap());
  out += payload;
  return out;
}

void ReliableLink::OnDatagram(const char *data, size_t len) {
  if (len < kHeaderBytes || data[0] != kMagic)
    return;
  const uint8_t type = (uint8_t)data[1];
  if (type != kTypeData && type != kTypeAck)
    return;
  const uint32_t seq = GetU32(data + 2);
  const uint32_t ack = GetU32(data + 6);
  const uint32_t bitmap = GetU32(data + 10);

  // The other side has everything before ack, plus the bitmap's packets.
  // (Ignore acknowledgements for packets never sent, e.g. stale ones.)
  if (!SeqBefore(next_seq_, ack)) {
    while (!in_flight_.empty() && SeqBefore(in_flight_.front().seq, ack)) {
      in_flight_bytes_ -= in_flight_.front().payload.size();
      in_flight_.pop_front();
    }
    for (size_t i = 0; i < in_flight_.size(); i++) {
      const uint32_t offset = in_flight_[i].seq - ack - 1;
      if (offset < 32 && (bitmap & (1u << offset))) {
        in_flight_[i].acked = true;
        if (!any_acked_ || SeqBefore(highest_acked_, in_flight_[i].seq))
          highest_acked_ = in_flight_[i].seq;
        any_acked_ = true;
      }
    }
  }

  if (type != kTypeData)
    return;
  // Acknowledge duplicates and gaps too, so the sender learns what's here.
  ack_due_ = true;
  if (seq == expected_seq_) {
    received_.append(data + kHeaderBytes, len - kHeaderBytes);
    expected_seq_++;
    // Packets that arrived early may now follow.
    std::map<uint32_t, std::string>::iterator it;
    while ((it = out_of_order_.find(expected_seq_)) != out_of_order_.end()) {
      received_ += it->second;
      out_of_order_.erase(it);
      expected_seq_++;
    }
  } else if (SeqBefore(expected_seq_, seq) &&
             seq - expected_seq_ <= 2 * kWindow) {
    out_of_order_[seq].assign(data + kHeaderBytes, len - kHeaderBytes);
  }
}

void ReliableLink::Resend(Packet *p, uint64_t now_ms,
                          std::vector<std::string> *out) {
  out->push_back(Encode(kTypeData, p->seq, p->payload));
  p->sent_ms = now_ms;
  resent_packets_++;
}

void ReliableLink::CollectOutgoing(uint64_t now_ms,
                                   std::vector<std::string> *out) {
  const size_t before = out->size();
  for (size_t i = 0; i < in_flight_.size(); i++) {
    Packet &p = in_flight_[i];
    if (p.acked)
      continue;
    if (now_ms - p.sent_ms >= kRetransmitMs) {
      Resend(&p, now_ms, out);
    } else if (!p.fast_resent && any_acked_ &&
               SeqBefore(p.seq, highest_acked_) &&
               highest_acked_ - p.seq >= kFastResendGap) {
      p.fast_resent = true;
      Resend(&p, now_ms, out);
    }
  }

  while (in_flight_.size() < kWindow && pending_offset_ < pending_.size()) {
    const size_t left = pending_.size() - pending_offset_;
    Packet p;
    p.seq = next_seq_++;
    p.payload = pending_.substr(pending_offset_,
                                left < kMaxPayload ? left : kMaxPayload);
    p.sent_ms = now_ms;
    p.acked = false;
    p.fast_resent = false;
    pending_offset_ += p.payload.size();
    in_flight_bytes_ += p.payload.size();
    out->push_back(Encode(kTypeData, p.seq, p.payload));
    in_flight_.push_back(p);
  }
  if (pending_offset_ == pending_.size()) {
    pending_.clear();
    pending_offset_ = 0;
  } else if (pending_offset_ > 256 * 1024) {
    pending_.erase(0, pending_offset_);
    pending_offset_ = 0;
  }

  // Data packets carry the acknowledgement; otherwise send one on its own.
  if (ack_due_ && out->size() == before)
    out->push_back(Encode(kTypeAck, 0, std::string()));
  ack_due_ = false;
}

std::string ReliableLink::TakeReceived() {
  std::string out;
  out.swap(received_);
  return out;
}
