#include "book/character_utils.h"

#include "test_assert.h"

#include <string>
#include <vector>

using character_utils::Mention;

namespace {

struct Pages {
  std::vector<std::vector<uint32_t> > pages;
  void Add(const std::string &utf8) {
    pages.push_back(annotation_text_utils::Utf8ToCodepoints(utf8));
  }
};

bool PageBuffer(void *ctx, int page, const uint32_t **buf, int *len) {
  Pages *p = (Pages *)ctx;
  if (page < 0 || page >= (int)p->pages.size())
    return false;
  *buf = p->pages[(size_t)page].data();
  *len = (int)p->pages[(size_t)page].size();
  return true;
}

std::vector<std::vector<Mention> > Find(Pages &pages,
                                        const std::vector<std::string> &names,
                                        int last_page, size_t max = 100) {
  std::vector<std::vector<Mention> > out;
  character_utils::FindMentions(names, PageBuffer, &pages, last_page, max,
                                &out);
  return out;
}

void TestNameKey() {
  test::ExpectStrEq("lower and trimmed",
                    character_utils::NameKey("  Sgt.\n Hughes ").c_str(),
                    "sgt. hughes");
  test::ExpectStrEq("curly apostrophe",
                    character_utils::NameKey("O\xE2\x80\x99" "Brien").c_str(),
                    "o'brien");
}

void TestWholeWordsAndCase() {
  Pages pages;
  pages.Add("Hughes ran. Then HUGHES, tired, sat down. Hughesville was far.");
  pages.Add("It was Hughes's rifle.");
  pages.Add("Later, Hughes again.");
  std::vector<std::string> names(1, "hughes");
  std::vector<std::vector<Mention> > out = Find(pages, names, 2);
  test::ExpectEq("count", (int)out[0].size(), 4);
  test::ExpectEq("first page", out[0][0].page, 0);
  test::ExpectStrEq("first sentence", out[0][0].snippet.c_str(),
                    "Hughes ran.");
  test::ExpectStrEq("second sentence", out[0][1].snippet.c_str(),
                    "Then HUGHES, tired, sat down.");
  test::ExpectEq("possessive", out[0][2].page, 1);
  test::ExpectStrEq("possessive snippet", out[0][2].snippet.c_str(),
                    "It was Hughes's rifle.");

  // Only up to the reader's page.
  out = Find(pages, names, 1);
  test::ExpectEq("spoiler-free", (int)out[0].size(), 3);
  out = Find(pages, names, 2, 2);
  test::ExpectEq("capped", (int)out[0].size(), 2);
}

void TestMultiWordNames() {
  Pages pages;
  pages.Add("Sgt. Dexter   Filkins arrived. Dexter left; Filkins stayed.");
  std::vector<std::string> names;
  names.push_back("Dexter Filkins");
  names.push_back("Filkins");
  std::vector<std::vector<Mention> > out = Find(pages, names, 0);
  test::ExpectEq("full name once", (int)out[0].size(), 1);
  // "Sgt." is an abbreviation, not the end of a sentence.
  test::ExpectStrEq("abbreviation kept", out[0][0].snippet.c_str(),
                    "Sgt. Dexter Filkins arrived.");
  test::ExpectEq("surname twice", (int)out[1].size(), 2);
  test::ExpectStrEq("second sentence", out[1][1].snippet.c_str(),
                    "Dexter left; Filkins stayed.");
}

void TestSnippetCut() {
  std::string long_text = "Start";
  for (int i = 0; i < 60; i++)
    long_text += " word";
  long_text += " Kaplan";
  for (int i = 0; i < 60; i++)
    long_text += " word";
  long_text += " end";
  Pages pages;
  pages.Add(long_text);
  pages.Add("\"Go home.\" Kaplan said. \"Now.\"");
  std::vector<std::string> names(1, "Kaplan");
  std::vector<std::vector<Mention> > out = Find(pages, names, 1);
  const std::string &s = out[0][0].snippet;
  test::ExpectTrue("cut at start", s.compare(0, 4, "...w") == 0);
  test::ExpectTrue("cut at end", s.size() > 3 &&
                                     s.compare(s.size() - 3, 3, "...") == 0);
  test::ExpectTrue("short enough", s.size() < 340);
  test::ExpectStrContains("has the name", s.c_str(), " Kaplan ");
  test::ExpectStrEq("quote ends a sentence", out[0][1].snippet.c_str(),
                    "Kaplan said.");
}

} // namespace

int main() {
  TestNameKey();
  TestWholeWordsAndCase();
  TestMultiWordNames();
  TestSnippetCut();
  return 0;
}
