/*
    3dslibris - stardict.cpp

    See include/dictionary/stardict.h.
*/

#include "dictionary/stardict.h"

#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "dictionary/word_lookup_utils.h"

namespace stardict {

namespace {

const size_t kSampleEvery = 32;
const uint32_t kMaxArticleBytes = 256 * 1024;

bool FileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ReadWholeFile(const std::string &path, size_t max_bytes,
                   std::string *out) {
  out->clear();
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp)
    return false;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0 && out->size() < max_bytes)
    out->append(buf, n);
  fclose(fp);
  return true;
}

uint32_t ReadBe32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int LowerAscii(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// Sequential reader of .idx entries from any entry start.
class IdxReader {
public:
  IdxReader(FILE *fp, long start, int offset_bits)
      : fp_(fp), offset_bytes_(offset_bits == 64 ? 8 : 4), file_pos_(start),
        begin_(0), end_(0), eof_(false) {
    fseek(fp_, start, SEEK_SET);
  }

  // False at the end of the file or on a malformed entry.
  bool Next(std::string *word, uint32_t *offset, uint32_t *size,
            long *entry_pos) {
    for (;;) {
      const char *data = buf_.data() + begin_;
      const size_t avail = end_ - begin_;
      const void *nul = memchr(data, '\0', avail);
      if (nul) {
        const size_t word_len = (size_t)((const char *)nul - data);
        const size_t need = word_len + 1 + offset_bytes_ + 4;
        if (avail >= need) {
          const unsigned char *num =
              (const unsigned char *)data + word_len + 1;
          word->assign(data, word_len);
          *offset = offset_bytes_ == 8 ? ReadBe32(num + 4) : ReadBe32(num);
          *size = ReadBe32(num + offset_bytes_);
          *entry_pos = file_pos_;
          begin_ += need;
          file_pos_ += (long)need;
          return true;
        }
      }
      if (eof_ || avail > 1024)
        return false; // no headword is this long: not an .idx
      Refill();
    }
  }

private:
  void Refill() {
    const size_t avail = end_ - begin_;
    std::vector<char> next(avail + kChunk);
    if (avail)
      memcpy(next.data(), buf_.data() + begin_, avail);
    const size_t n = fread(next.data() + avail, 1, kChunk, fp_);
    if (n < kChunk)
      eof_ = true;
    buf_.swap(next);
    begin_ = 0;
    end_ = avail + n;
  }

  static const size_t kChunk = 32 * 1024;
  FILE *fp_;
  size_t offset_bytes_;
  long file_pos_;
  std::vector<char> buf_;
  size_t begin_;
  size_t end_;
  bool eof_;
};

void AppendField(char type, const std::string &content, std::string *out) {
  std::string text;
  switch (type) {
  case 'm':
  case 'l':
  case 'y':
  case 'k':
    text = word_lookup_utils::TidyText(content);
    break;
  case 't':
    text = "/" + content + "/";
    break;
  case 'g':
  case 'h':
  case 'x':
    text = word_lookup_utils::MarkupToText(content);
    break;
  default:
    return; // sounds, pictures, resource lists
  }
  if (text.empty())
    return;
  if (!out->empty())
    *out += '\n';
  *out += text;
}

} // namespace

bool ParseIfo(const std::string &text, Info *out) {
  *out = Info();
  if (text.compare(0, 24, "StarDict's dict ifo file") != 0)
    return false;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line[line.size() - 1] == '\r')
      line.erase(line.size() - 1);
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "bookname")
      out->bookname = value;
    else if (key == "wordcount")
      out->wordcount = (uint32_t)strtoul(value.c_str(), NULL, 10);
    else if (key == "idxfilesize")
      out->idxfilesize = (uint32_t)strtoul(value.c_str(), NULL, 10);
    else if (key == "idxoffsetbits")
      out->idxoffsetbits = atoi(value.c_str()) == 64 ? 64 : 32;
    else if (key == "sametypesequence")
      out->sametypesequence = value;
  }
  return true;
}

int CompareHeadwordsIgnoreCase(const char *a, const char *b) {
  for (;; a++, b++) {
    const int ca = LowerAscii((unsigned char)*a);
    const int cb = LowerAscii((unsigned char)*b);
    if (ca != cb || ca == 0)
      return ca - cb;
  }
}

int CompareHeadwords(const char *a, const char *b) {
  const int c = CompareHeadwordsIgnoreCase(a, b);
  return c != 0 ? c : strcmp(a, b);
}

std::string ArticleText(const std::string &data,
                        const std::string &sametypesequence) {
  std::string out;
  size_t pos = 0;
  const size_t n = data.size();
  if (!sametypesequence.empty()) {
    for (size_t i = 0; i < sametypesequence.size() && pos < n; i++) {
      const char type = sametypesequence[i];
      std::string content;
      if (i + 1 == sametypesequence.size()) {
        content = data.substr(pos);
        pos = n;
      } else if (islower((unsigned char)type)) {
        size_t z = data.find('\0', pos);
        if (z == std::string::npos)
          z = n;
        content = data.substr(pos, z - pos);
        pos = z + 1;
      } else {
        if (pos + 4 > n)
          break;
        const uint32_t len = ReadBe32((const unsigned char *)data.data() + pos);
        pos += 4;
        content = data.substr(pos, len);
        pos += len;
      }
      AppendField(type, content, &out);
    }
    return out;
  }
  while (pos < n) {
    const char type = data[pos++];
    std::string content;
    if (islower((unsigned char)type)) {
      size_t z = data.find('\0', pos);
      if (z == std::string::npos)
        z = n;
      content = data.substr(pos, z - pos);
      pos = z + 1;
    } else {
      if (pos + 4 > n)
        break;
      const uint32_t len = ReadBe32((const unsigned char *)data.data() + pos);
      pos += 4;
      content = data.substr(pos, len);
      pos += len;
    }
    AppendField(type, content, &out);
  }
  return out;
}

bool Dictionary::Open(const std::string &ifo_path, std::string *error) {
  ifo_path_ = ifo_path;
  std::string text;
  if (!ReadWholeFile(ifo_path, 64 * 1024, &text) || !ParseIfo(text, &info_)) {
    if (error)
      *error = "not a StarDict .ifo";
    return false;
  }
  const std::string base = ifo_path.size() > 4
                               ? ifo_path.substr(0, ifo_path.size() - 4)
                               : ifo_path;
  if (info_.bookname.empty()) {
    const size_t slash = base.find_last_of('/');
    info_.bookname = slash == std::string::npos ? base : base.substr(slash + 1);
  }
  idx_path_ = base + ".idx";
  if (!FileExists(idx_path_)) {
    if (error)
      *error = FileExists(base + ".idx.gz")
                   ? "compressed .idx.gz is not supported; gunzip it"
                   : "missing .idx";
    return false;
  }
  if (FileExists(base + ".dict.dz")) {
    dict_path_ = base + ".dict.dz";
    dictzip_ = true;
    FILE *fp = fopen(dict_path_.c_str(), "rb");
    const bool ok = fp && ReadDictzipHeader(fp);
    if (fp)
      fclose(fp);
    if (!ok) {
      if (error)
        *error = "the .dict.dz is not dictzip (gunzip it to .dict)";
      return false;
    }
  } else if (FileExists(base + ".dict")) {
    dict_path_ = base + ".dict";
    dictzip_ = false;
  } else {
    if (error)
      *error = "missing .dict or .dict.dz";
    return false;
  }
  return true;
}

bool Dictionary::ReadDictzipHeader(FILE *fp) {
  unsigned char head[10];
  if (fread(head, 1, sizeof(head), fp) != sizeof(head) || head[0] != 0x1f ||
      head[1] != 0x8b || head[2] != 8 || !(head[3] & 0x04))
    return false;
  const int flags = head[3];
  unsigned char xlen_buf[2];
  if (fread(xlen_buf, 1, 2, fp) != 2)
    return false;
  const size_t xlen = xlen_buf[0] | (xlen_buf[1] << 8);
  std::vector<unsigned char> extra(xlen);
  if (xlen && fread(extra.data(), 1, xlen, fp) != xlen)
    return false;
  std::vector<uint32_t> sizes;
  for (size_t p = 0; p + 4 <= xlen;) {
    const size_t len = extra[p + 2] | (extra[p + 3] << 8);
    if (p + 4 + len > xlen)
      return false;
    if (extra[p] == 'R' && extra[p + 1] == 'A' && len >= 6) {
      const unsigned char *ra = &extra[p + 4];
      chunk_length_ = ra[2] | (ra[3] << 8);
      const size_t count = ra[4] | (ra[5] << 8);
      if (6 + count * 2 > len)
        return false;
      for (size_t i = 0; i < count; i++)
        sizes.push_back(ra[6 + i * 2] | (ra[7 + i * 2] << 8));
    }
    p += 4 + len;
  }
  if (chunk_length_ == 0 || sizes.empty())
    return false;
  // File name and comment are zero-terminated; the header CRC is 2 bytes.
  for (int bit = 0x08; bit <= 0x10; bit <<= 1) {
    if (!(flags & bit))
      continue;
    int c;
    while ((c = fgetc(fp)) != EOF && c != 0) {
    }
  }
  if (flags & 0x02)
    fseek(fp, 2, SEEK_CUR);
  long pos = ftell(fp);
  chunk_offsets_.assign(1, pos);
  for (size_t i = 0; i < sizes.size(); i++) {
    pos += (long)sizes[i];
    chunk_offsets_.push_back(pos);
  }
  return true;
}

bool Dictionary::LoadSamples() {
  samples_loaded_ = true;
  samples_.clear();
  FILE *fp = fopen(idx_path_.c_str(), "rb");
  if (!fp)
    return false;
  IdxReader reader(fp, 0, info_.idxoffsetbits);
  std::string word;
  uint32_t offset = 0;
  uint32_t size = 0;
  long pos = 0;
  size_t count = 0;
  while (reader.Next(&word, &offset, &size, &pos)) {
    if (count++ % kSampleEvery == 0) {
      Sample s;
      s.word = word;
      s.file_pos = pos;
      samples_.push_back(s);
    }
  }
  fclose(fp);
  return !samples_.empty();
}

bool Dictionary::ReadData(uint32_t offset, uint32_t size, std::string *out) {
  out->clear();
  if (size == 0)
    return true;
  if (size > kMaxArticleBytes)
    size = kMaxArticleBytes;
  FILE *fp = fopen(dict_path_.c_str(), "rb");
  if (!fp)
    return false;
  bool ok = true;
  if (!dictzip_) {
    out->resize(size);
    ok = fseek(fp, (long)offset, SEEK_SET) == 0 &&
         fread(&(*out)[0], 1, size, fp) == size;
  } else {
    const size_t chunk_count = chunk_offsets_.size() - 1;
    const size_t first = offset / chunk_length_;
    const size_t last = (offset + size - 1) / chunk_length_;
    std::string plain;
    std::vector<unsigned char> packed;
    std::vector<unsigned char> unpacked(chunk_length_);
    for (size_t c = first; ok && c <= last; c++) {
      if (c >= chunk_count) {
        ok = false;
        break;
      }
      const size_t packed_len =
          (size_t)(chunk_offsets_[c + 1] - chunk_offsets_[c]);
      packed.resize(packed_len);
      if (fseek(fp, chunk_offsets_[c], SEEK_SET) != 0 ||
          fread(packed.data(), 1, packed_len, fp) != packed_len) {
        ok = false;
        break;
      }
      z_stream zs;
      memset(&zs, 0, sizeof(zs));
      if (inflateInit2(&zs, -15) != Z_OK) {
        ok = false;
        break;
      }
      zs.next_in = packed.data();
      zs.avail_in = (uInt)packed_len;
      zs.next_out = unpacked.data();
      zs.avail_out = (uInt)unpacked.size();
      const int rc = inflate(&zs, Z_SYNC_FLUSH);
      const size_t produced = unpacked.size() - zs.avail_out;
      inflateEnd(&zs);
      if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
        ok = false;
        break;
      }
      plain.append((const char *)unpacked.data(), produced);
    }
    const size_t skip = offset - first * chunk_length_;
    if (ok && plain.size() >= skip)
      *out = plain.substr(skip, size);
    else
      ok = false;
  }
  fclose(fp);
  return ok;
}

bool Dictionary::Lookup(const std::string &word, size_t max_results,
                        std::vector<Article> *out) {
  if (word.empty() || max_results == 0)
    return true;
  if (!samples_loaded_)
    LoadSamples();
  if (samples_.empty())
    return false;
  // Last sample before the word; matches start at or after it.
  size_t lo = 0;
  size_t hi = samples_.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (CompareHeadwordsIgnoreCase(samples_[mid].word.c_str(), word.c_str()) <
        0)
      lo = mid + 1;
    else
      hi = mid;
  }
  const long start = lo == 0 ? 0 : samples_[lo - 1].file_pos;

  FILE *fp = fopen(idx_path_.c_str(), "rb");
  if (!fp)
    return false;
  IdxReader reader(fp, start, info_.idxoffsetbits);
  std::vector<Article> exact;
  std::vector<Article> other_case;
  std::string key;
  uint32_t offset = 0;
  uint32_t size = 0;
  long pos = 0;
  bool ok = true;
  while (reader.Next(&key, &offset, &size, &pos)) {
    const int c = CompareHeadwordsIgnoreCase(key.c_str(), word.c_str());
    if (c < 0)
      continue;
    if (c > 0)
      break;
    if (exact.size() + other_case.size() >= max_results * 2)
      continue;
    Article a;
    a.headword = key;
    a.offset = offset;
    std::string data;
    if (!ReadData(offset, size, &data)) {
      ok = false;
      continue;
    }
    a.text = ArticleText(data, info_.sametypesequence);
    (key == word ? exact : other_case).push_back(a);
  }
  fclose(fp);
  exact.insert(exact.end(), other_case.begin(), other_case.end());
  for (size_t i = 0; i < exact.size() && i < max_results; i++)
    out->push_back(exact[i]);
  return ok;
}

} // namespace stardict
