/*
    3dslibris - stardict.h

    Reads StarDict dictionaries (.ifo + .idx + .dict or dictzip .dict.dz),
    the format KOReader and most offline dictionary converters use. The
    .idx is scanned once on the first lookup to keep every 32nd headword
    in memory; a lookup then reads a few dozen entries from the card and
    inflates only the dictzip chunks holding the article. No 3DS
    dependencies (stdio + zlib), so it is tested on the host.
*/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace stardict {

struct Info {
  std::string bookname;
  uint32_t wordcount = 0;
  uint32_t idxfilesize = 0;
  int idxoffsetbits = 32;
  // One type letter per article field ("m" = plain text, "h" = HTML, ...).
  // Empty: each field starts with its own type letter.
  std::string sametypesequence;
};

// Parses an .ifo file's text. False if it isn't a StarDict .ifo.
bool ParseIfo(const std::string &text, Info *out);

// StarDict's headword order: ASCII case-insensitive, then byte order.
int CompareHeadwords(const char *a, const char *b);
// Only the case-insensitive part: 0 for "Dog" and "dog".
int CompareHeadwordsIgnoreCase(const char *a, const char *b);

// Turns an article's raw data into plain text with '\n' line breaks:
// text fields as they are, HTML/Pango/XDXF fields without their markup,
// sounds and pictures dropped.
std::string ArticleText(const std::string &data,
                        const std::string &sametypesequence);

struct Article {
  std::string headword;
  std::string text;
  uint32_t offset = 0; // in the .dict data; identifies the article
};

class Dictionary {
public:
  // ifo_path: the .ifo; the .idx and .dict(.dz) sit next to it.
  bool Open(const std::string &ifo_path, std::string *error);
  const std::string &name() const { return info_.bookname; }
  const std::string &ifo_path() const { return ifo_path_; }

  // Articles whose headword equals word ignoring ASCII case, exact case
  // first. Appends at most max_results. False on a read error.
  bool Lookup(const std::string &word, size_t max_results,
              std::vector<Article> *out);

private:
  struct Sample {
    std::string word;
    long file_pos;
  };
  bool LoadSamples();
  bool ReadData(uint32_t offset, uint32_t size, std::string *out);
  bool ReadDictzipHeader(FILE *fp);

  std::string ifo_path_;
  std::string idx_path_;
  std::string dict_path_;
  bool dictzip_ = false;
  Info info_;
  bool samples_loaded_ = false;
  std::vector<Sample> samples_;
  // dictzip: uncompressed chunk length and each chunk's file offset.
  uint32_t chunk_length_ = 0;
  std::vector<long> chunk_offsets_; // one more than the chunk count
};

} // namespace stardict
