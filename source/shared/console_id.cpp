#include "shared/console_id.h"

#include <stdio.h>
#include <time.h>

#include "shared/path_constants.h"

#ifdef __3DS__
#include <3ds.h>
#endif

namespace console_id {

namespace {

uint64_t Entropy() {
  uint64_t e = (uint64_t)time(NULL);
#ifdef __3DS__
  e ^= svcGetSystemTick() << 17;
  e ^= osGetTime() << 3;
#else
  e ^= (uint64_t)clock() << 21;
#endif
  return e;
}

std::string FilePath() { return paths::GetSdmcBase() + "/console_id.txt"; }

} // namespace

uint64_t Generate(uint64_t entropy) {
  // splitmix64: spreads low-entropy inputs (clock values) over all bits.
  uint64_t z = entropy + 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return z ? z : 1;
}

uint32_t PrefixFor(uint64_t id) {
  const uint32_t prefix = (uint32_t)(id >> 32) ^ (uint32_t)id;
  return prefix ? prefix : 1;
}

std::string Format(uint64_t id) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)id);
  return buf;
}

bool Parse(const std::string &text, uint64_t *out) {
  if (!out)
    return false;
  uint64_t v = 0;
  int digits = 0;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    int d;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    else if (c == '\r' || c == '\n' || c == ' ')
      continue;
    else
      return false;
    if (++digits > 16)
      return false;
    v = (v << 4) | (uint64_t)d;
  }
  if (digits != 16 || v == 0)
    return false;
  *out = v;
  return true;
}

uint64_t Get() {
  static uint64_t cached = 0;
  if (cached)
    return cached;

  const std::string path = FilePath();
  FILE *fp = fopen(path.c_str(), "rb");
  if (fp) {
    char buf[64] = {0};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    uint64_t id = 0;
    if (Parse(std::string(buf, n), &id)) {
      cached = id;
      return cached;
    }
  }

  cached = Generate(Entropy());
  fp = fopen(path.c_str(), "wb");
  if (fp) {
    const std::string text = Format(cached) + "\n";
    fwrite(text.data(), 1, text.size(), fp);
    fclose(fp);
  }
  return cached;
}

uint32_t Prefix() { return PrefixFor(Get()); }

} // namespace console_id
