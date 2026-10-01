#include "shared/console_id.h"

#include "test_assert.h"

namespace {

void TestGenerate() {
  const uint64_t a = console_id::Generate(1700000000ull);
  const uint64_t b = console_id::Generate(1700000001ull);
  test::ExpectTrue("non-zero", a != 0 && b != 0);
  test::ExpectTrue("nearby seeds differ", a != b);
  test::ExpectTrue("high bits used", (a >> 32) != 0 && (b >> 32) != 0);
  test::ExpectTrue("deterministic", console_id::Generate(42) ==
                                        console_id::Generate(42));
}

void TestPrefix() {
  test::ExpectTrue("prefix never zero",
                   console_id::PrefixFor(0x1234567812345678ull) != 0);
  test::ExpectTrue("prefix differs per id",
                   console_id::PrefixFor(console_id::Generate(1)) !=
                       console_id::PrefixFor(console_id::Generate(2)));
}

void TestFormatParse() {
  const uint64_t id = 0x00ab00cd12345678ull;
  const std::string text = console_id::Format(id);
  test::ExpectStrEq("16 hex digits", text.c_str(), "00ab00cd12345678");
  uint64_t parsed = 0;
  test::ExpectTrue("round trip", console_id::Parse(text + "\r\n", &parsed));
  test::ExpectTrue("same value", parsed == id);
  test::ExpectFalse("too short", console_id::Parse("abc", &parsed));
  test::ExpectFalse("not hex", console_id::Parse("zz00000000000000", &parsed));
  test::ExpectFalse("zero", console_id::Parse("0000000000000000", &parsed));
}

} // namespace

int main() {
  TestGenerate();
  TestPrefix();
  TestFormatParse();
  return 0;
}
