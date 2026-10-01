/*
    3dslibris - sync_protocol.h

    Messages exchanged during a sync, independent of the connection (Wi-Fi
    TCP or local wireless). Each message is one frame:

      "3LSY" | type (1) | 0 0 0 | payload length (4, little endian) |
      payload | CRC32 of type+reserved+length+payload (4, little endian)

    A session: both sides send HELLO (protocol version, pairing code, console
    id, display name) and MANIFEST; each merges the other's manifest into
    its own books. Missing books are then fetched with BOOK_REQUEST /
    FILE_CHUNK / FILE_END. DONE (sent when a side has finished its own
    requests) ends the session once both sides sent it; ABORT cancels it;
    PING keeps an idle connection from timing out.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace sync_protocol {

static const uint32_t kProtocolVersion = 1;
static const uint32_t kMaxPayloadBytes = 16u * 1024u * 1024u;
static const size_t kHeaderBytes = 12;
static const size_t kTrailerBytes = 4;

enum MessageType : uint8_t {
  kHello = 1,
  kManifest = 2,
  kBookRequest = 3,
  kFileChunk = 4,
  kFileEnd = 5,
  kDone = 6,
  kAbort = 7,
  kPing = 8, // keep-alive while a side is choosing books
};

struct Frame {
  uint8_t type;
  std::string payload;

  Frame() : type(0) {}
};

uint32_t Crc32(const void *data, size_t len, uint32_t crc = 0);

std::string EncodeFrame(uint8_t type, const std::string &payload);

// Accepts bytes as they arrive (any split) and returns whole frames.
class FrameDecoder {
public:
  enum Status { kOk, kError };

  FrameDecoder() : status_(kOk) {}
  // Appends bytes; complete frames are added to out. After a bad magic,
  // oversized length or CRC mismatch the decoder stays in kError.
  Status Feed(const void *data, size_t len, std::vector<Frame> *out);
  Status status() const { return status_; }
  size_t Buffered() const { return buffer_.size(); }

private:
  std::string buffer_;
  Status status_;
};

struct Hello {
  uint32_t protocol_version;
  std::string pairing_code;
  uint64_t console_id;
  std::string name;

  Hello() : protocol_version(kProtocolVersion), console_id(0) {}
};

std::string EncodeHello(const Hello &hello);
bool DecodeHello(const std::string &payload, Hello *out);

// BOOK_REQUEST: sync id and the byte offset to resume from.
std::string EncodeBookRequest(const std::string &sync_id, uint64_t offset);
bool DecodeBookRequest(const std::string &payload, std::string *sync_id,
                       uint64_t *offset);

// FILE_CHUNK: sync id, offset of this chunk, total size, data.
std::string EncodeFileChunk(const std::string &sync_id, uint64_t offset,
                            uint64_t total, const std::string &data);
bool DecodeFileChunk(const std::string &payload, std::string *sync_id,
                     uint64_t *offset, uint64_t *total, std::string *data);

// FILE_END: sync id and whether the whole file was sent.
enum FileEndStatus : uint32_t { kFileComplete = 0, kFileUnavailable = 1 };
std::string EncodeFileEnd(const std::string &sync_id, FileEndStatus status);
bool DecodeFileEnd(const std::string &payload, std::string *sync_id,
                   FileEndStatus *status);

} // namespace sync_protocol
