#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

#include "dictionary/dictionary_set.h"
#include "dictionary/stardict.h"
#include "dictionary/word_lookup_utils.h"
#include "test_assert.h"

#ifdef _WIN32
#include <direct.h>
static int MakeDir(const std::string &p) { return _mkdir(p.c_str()); }
#else
static int MakeDir(const std::string &p) { return mkdir(p.c_str(), 0755); }
#endif

using namespace word_lookup_utils;

static bool Contains(const std::vector<std::string> &v, const char *s) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == s)
      return true;
  return false;
}

static int OneEach(void *, uint32_t) { return 1; }

static void TestUtils() {
  test::ExpectStrEq("curly quotes and comma",
                    CleanSelectedWord("\xE2\x80\x9C" "Dog," "\xE2\x80\x9D").c_str(),
                    "Dog");
  test::ExpectStrEq("curly apostrophe",
                    CleanSelectedWord("don\xE2\x80\x99t.").c_str(), "don't");
  test::ExpectStrEq("soft hyphen dropped",
                    CleanSelectedWord("ex\xC2\xAD" "ample").c_str(), "example");
  test::ExpectStrEq("dash only", CleanSelectedWord("\xE2\x80\x94").c_str(), "");
  test::ExpectStrEq("inner hyphen kept",
                    CleanSelectedWord("(well-known)").c_str(), "well-known");
  test::ExpectStrEq("join hyphenated", JoinHyphenated("exam-", "ple").c_str(),
                    "example");
  test::ExpectStrEq("join soft hyphen",
                    JoinHyphenated("exam\xC2\xAD", "ple").c_str(), "example");
  test::ExpectStrEq("no hyphen", JoinHyphenated("exam", "ple").c_str(), "");
  test::ExpectStrEq("lower accented", ToLower("\xC3\x89" "COLE").c_str(),
                    "\xC3\xA9" "cole");

  std::vector<std::string> f = BaseFormCandidates("running");
  test::ExpectTrue("running -> run", Contains(f, "run"));
  test::ExpectFalse("no word itself", Contains(f, "running"));
  f = BaseFormCandidates("ponies");
  test::ExpectStrEq("ponies -> pony first", f[0].c_str(), "pony");
  test::ExpectTrue("dog's -> dog", Contains(BaseFormCandidates("dog's"), "dog"));
  test::ExpectTrue("dogs' -> dog", Contains(BaseFormCandidates("dogs'"), "dog"));
  test::ExpectFalse("glass is no plural",
                    Contains(BaseFormCandidates("glass"), "glas"));
  test::ExpectTrue("happily -> happy",
                   Contains(BaseFormCandidates("happily"), "happy"));
  test::ExpectTrue("bigger -> big", Contains(BaseFormCandidates("bigger"), "big"));
  test::ExpectTrue("baked -> bake", Contains(BaseFormCandidates("baked"), "bake"));
  test::ExpectTrue("boxes -> box", Contains(BaseFormCandidates("boxes"), "box"));
  test::ExpectTrue("dying -> die", Contains(BaseFormCandidates("dying"), "die"));

  test::ExpectStrEq(
      "markup",
      MarkupToText("<b>a</b>  b<br>c<ul><li>x</li><li>y</li></ul>&amp;&#233;"
                   "<style>p{}</style>&#x21;")
          .c_str(),
      "a b\nc\n\xE2\x80\xA2 x\n\xE2\x80\xA2 y\n&\xC3\xA9!");
  test::ExpectStrEq(
      "wiktionary empty item",
      MarkupToText("Canidae:\n<ol><li class=\"mw-empty-elt\"></li><li> The "
                   "<a href=\"/wiki/species\">species</a></li></ol>")
          .c_str(),
      "Canidae:\n\xE2\x80\xA2 The species");
  std::vector<WrappedLine> w = WrapText("1. aaaa bbbb cccc\n\nabcdefghijklmnop",
                                        10, OneEach, NULL);
  test::ExpectEq("wrap count", (int)w.size(), 6);
  test::ExpectStrEq("wrap 0", w[0].text.c_str(), "1. aaaa");
  test::ExpectEq("wrap 0 indent", w[0].indent_px, 0);
  test::ExpectStrEq("wrap 1", w[1].text.c_str(), "bbbb");
  test::ExpectEq("hanging indent", w[1].indent_px, 3);
  test::ExpectStrEq("wrap 2", w[2].text.c_str(), "cccc");
  test::ExpectStrEq("blank kept", w[3].text.c_str(), "");
  test::ExpectStrEq("hard break", w[4].text.c_str(), "abcdefghij");
  test::ExpectStrEq("hard break rest", w[5].text.c_str(), "klmnop");
  test::ExpectStrEq("tidy", TidyText("  a \n\n\n b\n").c_str(), "a\n\nb");
  test::ExpectStrEq("url encode", UrlEncode("caf\xC3\xA9 au/lait").c_str(),
                    "caf%C3%A9%20au%2Flait");
  test::ExpectStrEq("wikipedia url", WikipediaSummaryUrl("New York").c_str(),
                    "https://en.wikipedia.org/api/rest_v1/page/summary/"
                    "New_York?redirect=true");
}

// --- A small StarDict dictionary written by the test ----------------------

struct Entry {
  std::string word;
  std::string data;
};

static void Write(const std::string &path, const std::string &data) {
  FILE *fp = fopen(path.c_str(), "wb");
  if (!fp)
    test::Fail("cannot write " + path);
  fwrite(data.data(), 1, data.size(), fp);
  fclose(fp);
}

static void PutBe32(std::string *s, uint32_t v) {
  *s += (char)(v >> 24);
  *s += (char)(v >> 16);
  *s += (char)(v >> 8);
  *s += (char)v;
}

static void PutLe16(std::string *s, unsigned v) {
  *s += (char)(v & 0xFF);
  *s += (char)(v >> 8);
}

// dictzip with tiny chunks, so articles span several of them.
static std::string Dictzip(const std::string &data, unsigned chunk) {
  std::vector<std::string> parts;
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  deflateInit2(&zs, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  for (size_t i = 0; i < data.size(); i += chunk) {
    const std::string in = data.substr(i, chunk);
    std::string out(chunk * 2 + 64, '\0');
    zs.next_in = (Bytef *)in.data();
    zs.avail_in = (uInt)in.size();
    zs.next_out = (Bytef *)&out[0];
    zs.avail_out = (uInt)out.size();
    deflate(&zs, i + chunk >= data.size() ? Z_FINISH : Z_FULL_FLUSH);
    out.resize(out.size() - zs.avail_out);
    parts.push_back(out);
  }
  deflateEnd(&zs);
  std::string ra;
  PutLe16(&ra, 1);
  PutLe16(&ra, chunk);
  PutLe16(&ra, (unsigned)parts.size());
  for (size_t i = 0; i < parts.size(); i++)
    PutLe16(&ra, (unsigned)parts[i].size());
  std::string extra = "RA";
  PutLe16(&extra, (unsigned)ra.size());
  extra += ra;
  std::string file("\x1f\x8b\x08\x0c\0\0\0\0\x02\x03", 10); // FEXTRA|FNAME
  PutLe16(&file, (unsigned)extra.size());
  file += extra;
  file += std::string("words.dict\0", 11);
  for (size_t i = 0; i < parts.size(); i++)
    file += parts[i];
  file += std::string(8, '\0'); // CRC and size: not checked
  return file;
}

static void WriteDictionary(const std::string &dir, const std::string &name,
                            std::vector<Entry> entries, const char *types,
                            bool dictzip) {
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return stardict::CompareHeadwords(a.word.c_str(), b.word.c_str()) < 0;
  });
  std::string idx;
  std::string dict;
  for (size_t i = 0; i < entries.size(); i++) {
    idx += entries[i].word;
    idx += '\0';
    PutBe32(&idx, (uint32_t)dict.size());
    PutBe32(&idx, (uint32_t)entries[i].data.size());
    dict += entries[i].data;
  }
  const std::string base = dir + "/" + name;
  std::string ifo = "StarDict's dict ifo file\nversion=2.4.2\nbookname=" + name +
                    "\nwordcount=" + std::to_string(entries.size()) + "\n";
  if (types[0])
    ifo += std::string("sametypesequence=") + types + "\n";
  Write(base + ".ifo", ifo);
  Write(base + ".idx", idx);
  if (dictzip)
    Write(base + ".dict.dz", Dictzip(dict, 64));
  else
    Write(base + ".dict", dict);
}

static std::vector<Entry> SampleEntries() {
  std::vector<Entry> e;
  e.push_back({"apple", "a round fruit that grows on trees and is often red "
                        "or green; long enough to cross several chunks"});
  e.push_back({"bank", "lower-case bank article"});
  e.push_back({"Bank", "capitalised Bank article"});
  e.push_back({"run", "move fast on foot"});
  e.push_back({"zebra", "striped horse"});
  for (int i = 0; i < 100; i++) {
    char w[16];
    snprintf(w, sizeof(w), "w%04d", i);
    e.push_back({w, std::string("filler ") + w});
  }
  return e;
}

static void TestStarDict(const std::string &root) {
  stardict::Info info;
  test::ExpectTrue("ifo parse",
                   stardict::ParseIfo("StarDict's dict ifo file\r\nbookname=X"
                                      "\r\nsametypesequence=h\r\n",
                                      &info));
  test::ExpectStrEq("ifo name", info.bookname.c_str(), "X");
  test::ExpectStrEq("ifo types", info.sametypesequence.c_str(), "h");
  test::ExpectFalse("not ifo", stardict::ParseIfo("hello", &info));
  test::ExpectLt("order ignores case",
                 stardict::CompareHeadwords("apple", "Bank"), 0);
  test::ExpectLt("upper first on tie", stardict::CompareHeadwords("Bank", "bank"),
                 0);

  std::string typed = "mmeaning";
  typed += '\0';
  typed += "tpho";
  typed += '\0';
  test::ExpectStrEq("typed fields",
                    stardict::ArticleText(typed, "").c_str(), "meaning\n/pho/");
  test::ExpectStrEq("html field",
                    stardict::ArticleText("<i>a</i><br/>b", "h").c_str(), "a\nb");

  for (int z = 0; z < 2; z++) {
    const std::string dir = root + (z ? "/dz" : "/plain");
    MakeDir(dir);
    WriteDictionary(dir, "sample", SampleEntries(), "m", z == 1);
    stardict::Dictionary d;
    std::string error;
    test::ExpectTrue("open", d.Open(dir + "/sample.ifo", &error));
    std::vector<stardict::Article> out;
    test::ExpectTrue("lookup apple", d.Lookup("apple", 5, &out));
    test::ExpectEq("one apple", (int)out.size(), 1);
    test::ExpectStrContains("apple text", out[0].text.c_str(),
                            "several chunks");
    out.clear();
    d.Lookup("bank", 5, &out);
    test::ExpectEq("two banks", (int)out.size(), 2);
    test::ExpectStrEq("exact case first", out[0].headword.c_str(), "bank");
    out.clear();
    d.Lookup("BANK", 5, &out);
    test::ExpectEq("case-insensitive", (int)out.size(), 2);
    out.clear();
    d.Lookup("w0077", 5, &out);
    test::ExpectEq("filler found", (int)out.size(), 1);
    test::ExpectStrEq("filler text", out[0].text.c_str(), "filler w0077");
    out.clear();
    d.Lookup("zebra", 5, &out);
    test::ExpectStrEq("last entry", out.at(0).text.c_str(), "striped horse");
    out.clear();
    d.Lookup("aardvark", 5, &out);
    d.Lookup("nope", 5, &out);
    d.Lookup("zzz", 5, &out);
    test::ExpectEq("missing words", (int)out.size(), 0);
  }

  // A plain gzip (no random access) is refused with a hint.
  const std::string bad = root + "/bad";
  MakeDir(bad);
  WriteDictionary(bad, "gz", SampleEntries(), "m", false);
  Write(bad + "/gz.dict.dz", std::string("\x1f\x8b\x08\0\0\0\0\0\0\x03", 10));
  stardict::Dictionary d;
  std::string error;
  test::ExpectFalse("plain gzip refused", d.Open(bad + "/gz.ifo", &error));
  test::ExpectStrContains("gzip hint", error.c_str(), "gunzip");
}

static void TestDictionarySet(const std::string &root) {
  const std::string dir = root + "/set";
  MakeDir(dir);
  MakeDir(dir + "/sub");
  WriteDictionary(dir + "/sub", "sample", SampleEntries(), "m", true);
  std::vector<Entry> html;
  html.push_back({"run", "<b>run</b>: to go quickly"});
  WriteDictionary(dir, "html", html, "h", false);

  dictionary::DictionarySet set;
  set.AddDirectory(dir);
  set.AddDirectory(root + "/plain"); // another sample.ifo: skipped
  test::ExpectEq("two dictionaries", (int)set.size(), 2);

  std::vector<dictionary::Result> r = set.Lookup("Running", 6);
  test::ExpectEq("running -> run in both", (int)r.size(), 2);
  test::ExpectStrEq("html stripped", r[0].text.c_str(), "run: to go quickly");
  test::ExpectStrEq("headword", r[1].headword.c_str(), "run");
  r = set.Lookup("Apple", 6);
  test::ExpectEq("apple", (int)r.size(), 1);
  r = set.Lookup("bank", 1);
  test::ExpectEq("max results", (int)r.size(), 1);
  test::ExpectEq("not found", (int)set.Lookup("qwerty", 6).size(), 0);
}

// The bundled WordNet, when present.
static void TestWordNet() {
  const std::string dir = std::string(TEST_ROOT_DIR) + "/sdmc/3ds/3dslibris/dict";
  struct stat st;
  if (stat((dir + "/wordnet/wordnet.ifo").c_str(), &st) != 0) {
    printf("  (bundled WordNet not found; skipped)\n");
    return;
  }
  dictionary::DictionarySet set;
  set.AddDirectory(dir);
  std::vector<dictionary::Result> r = set.Lookup("Mice", 4);
  test::ExpectTrue("mice found", !r.empty());
  test::ExpectStrContains("mice -> mouse", r[0].text.c_str(), "mouse\nnoun");
  r = set.Lookup("dogs", 4);
  test::ExpectTrue("dogs found", !r.empty());
  test::ExpectStrContains("dogs -> dog", r[0].text.c_str(), "dog\nnoun\n1. ");
  r = set.Lookup("went", 4);
  test::ExpectTrue("went found", !r.empty());
  test::ExpectStrContains("went -> go", r[0].text.c_str(), "verb");
}

int main() {
  const char *tmp = getenv("TMPDIR");
  const std::string root = std::string(tmp && tmp[0] ? tmp : "/tmp") +
                           "/3dslibris-word-lookup-" +
                           std::to_string((long long)getpid());
  MakeDir(root);
  TestUtils();
  TestStarDict(root);
  TestDictionarySet(root);
  TestWordNet();
  printf("test_word_lookup: OK\n");
  return 0;
}
