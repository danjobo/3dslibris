/*
    3dslibris - reliable_link.h

    A reliable, in-order byte stream over datagrams that may be lost,
    duplicated or reordered (local wireless delivers frames of at most
    ~1.4 KB and drops them when its receive buffer is full).

    The stream is cut into numbered packets; at most kWindow are
    unacknowledged at a time. The receiver keeps out-of-order packets and
    acknowledges with the next sequence number it needs plus a bitmap of
    the packets after it that already arrived. The sender resends only
    missing packets: when they are kRetransmitMs old, or as soon as three
    later packets were acknowledged.

    Packet: 'L' (1) | type (1) | seq (4) | ack (4) | received bitmap (4) |
    payload, little-endian. Pure logic with no I/O: the transport feeds
    received datagrams in and sends what CollectOutgoing() returns.
    Host-tested.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

class ReliableLink {
public:
  static const size_t kHeaderBytes = 14;
  static const size_t kMaxPayload = 1400;
  static const size_t kWindow = 16;
  static const uint64_t kRetransmitMs = 250;

  ReliableLink();

  // Queues stream bytes for sending.
  void Queue(const std::string &bytes);
  // Bytes not yet acknowledged by the other side (queued or in flight).
  size_t QueuedBytes() const;
  bool AllAcknowledged() const { return QueuedBytes() == 0; }

  // Handles one received datagram (ignored when malformed).
  void OnDatagram(const char *data, size_t len);
  // Datagrams to send now: new data within the window, resends, and an
  // acknowledgement when one is due.
  void CollectOutgoing(uint64_t now_ms, std::vector<std::string> *out);
  // Returns and clears the in-order stream bytes received so far.
  std::string TakeReceived();

  uint32_t resent_packets() const { return resent_packets_; }

private:
  struct Packet {
    uint32_t seq;
    std::string payload;
    uint64_t sent_ms;
    bool acked;        // arrived out of order at the other side
    bool fast_resent;  // already resent because later packets arrived
  };

  std::string Encode(uint8_t type, uint32_t seq, const std::string &payload)
      const;
  uint32_t ReceivedBitmap() const;
  void Resend(Packet *p, uint64_t now_ms, std::vector<std::string> *out);

  std::string pending_;
  size_t pending_offset_;
  std::deque<Packet> in_flight_;
  size_t in_flight_bytes_;
  uint32_t next_seq_;
  // Highest sequence number the other side reported (to find holes).
  uint32_t highest_acked_;
  bool any_acked_;

  uint32_t expected_seq_;
  std::map<uint32_t, std::string> out_of_order_;
  bool ack_due_;
  std::string received_;
  uint32_t resent_packets_;
};
