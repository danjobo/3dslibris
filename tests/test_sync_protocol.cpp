#include "sync/sync_protocol.h"

#include "test_assert.h"

#include <string>
#include <vector>

using sync_protocol::Frame;
using sync_protocol::FrameDecoder;

namespace {

void TestCrc32() {
  // Standard check value for "123456789".
  test::ExpectEqU("crc32 check value", sync_protocol::Crc32("123456789", 9),
                  0xCBF43926u);
  const uint32_t part = sync_protocol::Crc32("12345", 5);
  test::ExpectEqU("incremental", sync_protocol::Crc32("6789", 4, part),
                  0xCBF43926u);
}

void TestFramesSurviveAnySplit() {
  std::string payload(5000, 'x');
  for (size_t i = 0; i < payload.size(); i++)
    payload[i] = (char)(i * 31);
  const std::string stream =
      sync_protocol::EncodeFrame(sync_protocol::kManifest, payload) +
      sync_protocol::EncodeFrame(sync_protocol::kDone, "");

  // Byte by byte.
  FrameDecoder dec;
  std::vector<Frame> frames;
  for (size_t i = 0; i < stream.size(); i++)
    test::ExpectTrue("byte feed ok",
                     dec.Feed(stream.data() + i, 1, &frames) ==
                         FrameDecoder::kOk);
  test::ExpectEq("two frames", (int)frames.size(), 2);
  test::ExpectEq("type", frames[0].type, sync_protocol::kManifest);
  test::ExpectTrue("payload intact", frames[0].payload == payload);
  test::ExpectEq("empty payload frame", frames[1].type, sync_protocol::kDone);
  test::ExpectEq("nothing left", (int)dec.Buffered(), 0);

  // In packet-sized pieces, like local wireless.
  FrameDecoder dec2;
  frames.clear();
  for (size_t i = 0; i < stream.size(); i += 1400)
    dec2.Feed(stream.data() + i,
              std::min((size_t)1400, stream.size() - i), &frames);
  test::ExpectEq("chunked two frames", (int)frames.size(), 2);
}

void TestCorruptionIsRejected() {
  std::string frame = sync_protocol::EncodeFrame(sync_protocol::kHello, "abc");
  frame[13] ^= 0x01; // flip a payload bit
  FrameDecoder dec;
  std::vector<Frame> frames;
  test::ExpectTrue("crc error",
                   dec.Feed(frame.data(), frame.size(), &frames) ==
                       FrameDecoder::kError);
  test::ExpectEq("no frame", (int)frames.size(), 0);
  test::ExpectTrue("stays in error",
                   dec.Feed("x", 1, &frames) == FrameDecoder::kError);

  FrameDecoder bad_magic;
  test::ExpectTrue("bad magic",
                   bad_magic.Feed("HTTP/1.1 200 OK\r\n", 17, &frames) ==
                       FrameDecoder::kError);

  std::string huge = sync_protocol::EncodeFrame(sync_protocol::kHello, "");
  huge[8] = huge[9] = huge[10] = huge[11] = (char)0xFF; // 4 GB length
  FrameDecoder oversized;
  test::ExpectTrue("oversized length",
                   oversized.Feed(huge.data(), huge.size(), &frames) ==
                       FrameDecoder::kError);
}

void TestHello() {
  sync_protocol::Hello in;
  in.pairing_code = "4821";
  in.console_id = 0x1122334455667788ull;
  in.name = "Daniel's 3DS";
  sync_protocol::Hello out;
  test::ExpectTrue("decode", sync_protocol::DecodeHello(
                                 sync_protocol::EncodeHello(in), &out));
  test::ExpectEqU("version", out.protocol_version,
                  sync_protocol::kProtocolVersion);
  test::ExpectStrEq("code", out.pairing_code.c_str(), "4821");
  test::ExpectTrue("console id", out.console_id == in.console_id);
  test::ExpectStrEq("name", out.name.c_str(), "Daniel's 3DS");
  test::ExpectFalse("truncated",
                    sync_protocol::DecodeHello(std::string(5, '\0'), &out));
}

void TestBookTransferMessages() {
  std::string id;
  uint64_t offset = 0, total = 0;
  std::string data;
  test::ExpectTrue("request",
                   sync_protocol::DecodeBookRequest(
                       sync_protocol::EncodeBookRequest("book.epub#123", 64),
                       &id, &offset));
  test::ExpectStrEq("request id", id.c_str(), "book.epub#123");
  test::ExpectTrue("request offset", offset == 64);

  const std::string chunk(16384, 'z');
  test::ExpectTrue("chunk", sync_protocol::DecodeFileChunk(
                                sync_protocol::EncodeFileChunk(
                                    "book.epub#123", 16384, 100000, chunk),
                                &id, &offset, &total, &data));
  test::ExpectTrue("chunk fields",
                   offset == 16384 && total == 100000 && data == chunk);
  test::ExpectFalse("chunk past end rejected",
                    sync_protocol::DecodeFileChunk(
                        sync_protocol::EncodeFileChunk("x", 99990, 100000,
                                                       chunk),
                        &id, &offset, &total, &data));
}

} // namespace

int main() {
  TestCrc32();
  TestFramesSurviveAnySplit();
  TestCorruptionIsRejected();
  TestHello();
  TestBookTransferMessages();
  return 0;
}
