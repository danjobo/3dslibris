#include "sync/sync_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sync_protocol {

namespace {

static const char kMagic[4] = {'3', 'L', 'S', 'Y'};

void PutU32(std::string *out, uint32_t v) {
  for (int i = 0; i < 4; i++)
    out->push_back((char)((v >> (8 * i)) & 0xFF));
}

void PutU64(std::string *out, uint64_t v) {
  for (int i = 0; i < 8; i++)
    out->push_back((char)((v >> (8 * i)) & 0xFF));
}

uint32_t GetU32(const char *p) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; i--)
    v = (v << 8) | (unsigned char)p[i];
  return v;
}

uint64_t GetU64(const char *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--)
    v = (v << 8) | (unsigned char)p[i];
  return v;
}

void PutString(std::string *out, const std::string &s) {
  PutU32(out, (uint32_t)s.size());
  *out += s;
}

bool GetString(const std::string &in, size_t *pos, std::string *out) {
  if (*pos + 4 > in.size())
    return false;
  const uint32_t len = GetU32(in.data() + *pos);
  *pos += 4;
  if (len > in.size() - *pos)
    return false;
  out->assign(in, *pos, len);
  *pos += len;
  return true;
}

} // namespace

uint32_t Crc32(const void *data, size_t len, uint32_t crc) {
  static uint32_t table[256];
  static bool table_ready = false;
  if (!table_ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    table_ready = true;
  }
  const unsigned char *p = (const unsigned char *)data;
  crc = ~crc;
  for (size_t i = 0; i < len; i++)
    crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

std::string EncodeFrame(uint8_t type, const std::string &payload) {
  std::string out(kMagic, 4);
  out.push_back((char)type);
  out.append(3, '\0');
  PutU32(&out, (uint32_t)payload.size());
  out += payload;
  const uint32_t crc = Crc32(out.data() + 4, out.size() - 4);
  PutU32(&out, crc);
  return out;
}

FrameDecoder::Status FrameDecoder::Feed(const void *data, size_t len,
                                        std::vector<Frame> *out) {
  if (status_ != kOk)
    return status_;
  if (data && len)
    buffer_.append((const char *)data, len);
  while (buffer_.size() >= kHeaderBytes) {
    if (memcmp(buffer_.data(), kMagic, 4) != 0) {
      status_ = kError;
      return status_;
    }
    const uint32_t payload_len = GetU32(buffer_.data() + 8);
    if (payload_len > kMaxPayloadBytes) {
      status_ = kError;
      return status_;
    }
    const size_t total = kHeaderBytes + payload_len + kTrailerBytes;
    if (buffer_.size() < total)
      break;
    const uint32_t expected = GetU32(buffer_.data() + kHeaderBytes + payload_len);
    const uint32_t actual = Crc32(buffer_.data() + 4, kHeaderBytes - 4 + payload_len);
    if (expected != actual) {
      status_ = kError;
      return status_;
    }
    if (out) {
      Frame frame;
      frame.type = (uint8_t)buffer_[4];
      frame.payload.assign(buffer_, kHeaderBytes, payload_len);
      out->push_back(frame);
    }
    buffer_.erase(0, total);
  }
  return status_;
}

std::string EncodeHello(const Hello &hello) {
  std::string out;
  PutU32(&out, hello.protocol_version);
  PutU64(&out, hello.console_id);
  PutString(&out, hello.pairing_code);
  PutString(&out, hello.name);
  return out;
}

bool DecodeHello(const std::string &payload, Hello *out) {
  if (!out || payload.size() < 12)
    return false;
  out->protocol_version = GetU32(payload.data());
  out->console_id = GetU64(payload.data() + 4);
  size_t pos = 12;
  return GetString(payload, &pos, &out->pairing_code) &&
         GetString(payload, &pos, &out->name) && pos == payload.size();
}

std::string EncodeBookRequest(const std::string &sync_id, uint64_t offset) {
  std::string out;
  PutString(&out, sync_id);
  PutU64(&out, offset);
  return out;
}

bool DecodeBookRequest(const std::string &payload, std::string *sync_id,
                       uint64_t *offset) {
  if (!sync_id || !offset)
    return false;
  size_t pos = 0;
  if (!GetString(payload, &pos, sync_id) || pos + 8 != payload.size())
    return false;
  *offset = GetU64(payload.data() + pos);
  return true;
}

std::string EncodeFileChunk(const std::string &sync_id, uint64_t offset,
                            uint64_t total, const std::string &data) {
  std::string out;
  PutString(&out, sync_id);
  PutU64(&out, offset);
  PutU64(&out, total);
  PutString(&out, data);
  return out;
}

bool DecodeFileChunk(const std::string &payload, std::string *sync_id,
                     uint64_t *offset, uint64_t *total, std::string *data) {
  if (!sync_id || !offset || !total || !data)
    return false;
  size_t pos = 0;
  if (!GetString(payload, &pos, sync_id) || pos + 16 > payload.size())
    return false;
  *offset = GetU64(payload.data() + pos);
  *total = GetU64(payload.data() + pos + 8);
  pos += 16;
  if (!GetString(payload, &pos, data) || pos != payload.size())
    return false;
  return *offset <= *total && data->size() <= *total - *offset;
}

} // namespace sync_protocol
