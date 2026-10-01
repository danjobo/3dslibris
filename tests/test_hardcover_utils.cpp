#include "book/hardcover_utils.h"

#include "test_assert.h"

#include <string>
#include <vector>

using hardcover_utils::Link;

namespace {

void TestCleanToken() {
  test::ExpectStrEq("bearer prefix",
                    hardcover_utils::CleanToken("Bearer eyJabc.def \n").c_str(),
                    "eyJabc.def");
  test::ExpectStrEq("lowercase prefix",
                    hardcover_utils::CleanToken("bearer eyJx").c_str(), "eyJx");
  test::ExpectStrEq("bare token",
                    hardcover_utils::CleanToken(" eyJx.y.z ").c_str(),
                    "eyJx.y.z");
}

void TestIsbnFromFileName() {
  test::ExpectStrEq(
      "anna's archive name",
      hardcover_utils::IsbnFromFileName(
          "The Forever War -- Filkins, Dexter -- 2008 -- isbn13 "
          "9780307266392 -- 8ccf9242f912e8f72ffadff038614cb0 -- Anna.epub")
          .c_str(),
      "9780307266392");
  test::ExpectStrEq("hyphenated",
                    hardcover_utils::IsbnFromFileName("x 978-0-14-139727-6.epub")
                        .c_str(),
                    "9780141397276");
  test::ExpectStrEq("bad check digit",
                    hardcover_utils::IsbnFromFileName("9780307266393.epub")
                        .c_str(),
                    "");
  test::ExpectStrEq("part of a longer number",
                    hardcover_utils::IsbnFromFileName("19780307266392.epub")
                        .c_str(),
                    "");
  test::ExpectStrEq("none", hardcover_utils::IsbnFromFileName("Dune.epub").c_str(),
                    "");
}

void TestLinksRoundTrip() {
  std::vector<Link> links;
  Link a;
  a.sync_id = "Dune.epub#12345";
  a.book_id = 312460;
  a.edition_id = 30405274;
  a.pages = 658;
  a.title = "Dune\twith a tab";
  a.last_sent_page = 120;
  links.push_back(a);
  Link b;
  b.sync_id = "Other.pdf#9";
  b.book_id = 7;
  b.pages = 100;
  b.finished = true;
  links.push_back(b);
  const std::vector<Link> back =
      hardcover_utils::ParseLinks(hardcover_utils::SerializeLinks(links));
  test::ExpectEq("count", (int)back.size(), 2);
  test::ExpectEq("book id", back[0].book_id, 312460);
  test::ExpectEq("edition", back[0].edition_id, 30405274);
  test::ExpectEq("pages", back[0].pages, 658);
  test::ExpectEq("last sent", back[0].last_sent_page, 120);
  test::ExpectStrEq("title tab removed", back[0].title.c_str(),
                    "Dune with a tab");
  test::ExpectTrue("finished", back[1].finished);
  test::ExpectEq("find", hardcover_utils::FindLink(back, "Other.pdf#9"), 1);
  test::ExpectEq("missing", hardcover_utils::FindLink(back, "x#1"), -1);
  test::ExpectEq("junk", (int)hardcover_utils::ParseLinks("junk\n").size(), 0);
  test::ExpectEq("crlf file",
                 (int)hardcover_utils::ParseLinks(
                     "3DSLIBRIS-HARDCOVER 1\r\na#1\t5\t0\t10\t0\t0\tT\r\n")
                     .size(),
                 1);
}

void TestUnlinkedEntries() {
  std::vector<Link> links(1);
  links[0].sync_id = "a#1";
  links[0].changed_at = 500;
  links[0].title = "A";
  const std::vector<Link> back =
      hardcover_utils::ParseLinks(hardcover_utils::SerializeLinks(links));
  test::ExpectEq("unlinked kept", (int)back.size(), 1);
  test::ExpectEq("changed_at", (int)back[0].changed_at, 500);
  test::ExpectEq("not a link", hardcover_utils::FindLink(back, "a#1"), -1);
  test::ExpectEq("but an entry", hardcover_utils::FindEntry(back, "a#1"), 0);
  test::ExpectEq("v1 rejects book 0",
                 (int)hardcover_utils::ParseLinks(
                     "3DSLIBRIS-HARDCOVER 1\na#1\t0\t0\t10\t0\t0\tT\n")
                     .size(),
                 0);
}

Link MakeLink(const char *sync_id, int book_id, uint32_t changed_at,
              int last_sent = 0) {
  Link l;
  l.sync_id = sync_id;
  l.book_id = book_id;
  l.pages = 300;
  l.title = "T";
  l.changed_at = changed_at;
  l.last_sent_page = last_sent;
  return l;
}

void TestMergeLinks() {
  // New link from the other console.
  std::vector<Link> mine;
  std::vector<Link> theirs(1, MakeLink("a#1", 7, 100));
  test::ExpectEq("new link counted", hardcover_utils::MergeLinks(&mine, theirs),
                 1);
  test::ExpectEq("now linked", hardcover_utils::FindLink(mine, "a#1"), 0);

  // A newer relink wins; an older one doesn't.
  mine.assign(1, MakeLink("a#1", 7, 100));
  theirs.assign(1, MakeLink("a#1", 8, 200));
  test::ExpectEq("relink", hardcover_utils::MergeLinks(&mine, theirs), 1);
  test::ExpectEq("newer book", mine[0].book_id, 8);
  theirs.assign(1, MakeLink("a#1", 9, 150));
  test::ExpectEq("older ignored", hardcover_utils::MergeLinks(&mine, theirs),
                 0);
  test::ExpectEq("kept", mine[0].book_id, 8);

  // A newer unlink removes the link; an unlink of an unknown book is kept
  // quietly.
  theirs.assign(1, MakeLink("a#1", 0, 300));
  test::ExpectEq("unlink", hardcover_utils::MergeLinks(&mine, theirs), 1);
  test::ExpectEq("unlinked", hardcover_utils::FindLink(mine, "a#1"), -1);
  theirs.assign(1, MakeLink("b#1", 0, 300));
  test::ExpectEq("unknown unlink", hardcover_utils::MergeLinks(&mine, theirs),
                 0);

  // The same link: furthest progress, either side's Read.
  mine.assign(1, MakeLink("a#1", 7, 100, 40));
  theirs.assign(1, MakeLink("a#1", 7, 100, 90));
  theirs[0].finished = true;
  test::ExpectEq("progress not counted",
                 hardcover_utils::MergeLinks(&mine, theirs), 0);
  test::ExpectEq("furthest page", mine[0].last_sent_page, 90);
  test::ExpectTrue("read", mine[0].finished);

  // Both directions agree when linked separately at the same time.
  std::vector<Link> x(1, MakeLink("a#1", 7, 100));
  std::vector<Link> y(1, MakeLink("a#1", 8, 100));
  std::vector<Link> x2 = x, y2 = y;
  hardcover_utils::MergeLinks(&x2, y);
  hardcover_utils::MergeLinks(&y2, x);
  test::ExpectStrEq("converge", hardcover_utils::SerializeLinks(x2).c_str(),
                    hardcover_utils::SerializeLinks(y2).c_str());
}

void TestProgressPage() {
  // Halfway through 200 local pages of a 400-page book.
  test::ExpectEq("half", hardcover_utils::ProgressPage(99, 200, 400), 200);
  test::ExpectEq("first page", hardcover_utils::ProgressPage(0, 200, 400), 2);
  test::ExpectEq("at least 1", hardcover_utils::ProgressPage(0, 1000, 100), 1);
  test::ExpectEq("last page is the end",
                 hardcover_utils::ProgressPage(199, 200, 400), 400);
  test::ExpectEq("unknown pages", hardcover_utils::ProgressPage(5, 200, 0), 0);
  test::ExpectTrue("finished", hardcover_utils::IsFinished(199, 200));
  test::ExpectFalse("not finished", hardcover_utils::IsFinished(198, 200));
}

void TestBodies() {
  test::ExpectStrEq("date", hardcover_utils::DateString(1790858096u).c_str(),
                    "2026-10-01");
  test::ExpectStrEq(
      "me", hardcover_utils::MeBody().c_str(),
      "{\"query\":\"{ me { id account_privacy_setting_id } }\","
      "\"variables\":{}}");
  const std::string isbn = hardcover_utils::EditionByIsbnBody("9780307266392");
  test::ExpectTrue("isbn variable",
                   isbn.find("\"variables\":{\"isbn\":\"9780307266392\"}") !=
                       std::string::npos);
  const std::string search =
      hardcover_utils::SearchBody("The \"Forever\" War Filkins", 8);
  test::ExpectTrue("search escapes quotes",
                   search.find("\"query\":\"The \\\"Forever\\\" War Filkins\"") !=
                       std::string::npos);
  std::vector<int> ids;
  ids.push_back(3);
  ids.push_back(14);
  test::ExpectTrue("ids list",
                   hardcover_utils::BooksByIdsBody(ids).find(
                       "{\"ids\":[3,14]}") != std::string::npos);
  test::ExpectTrue(
      "status object",
      hardcover_utils::SetStatusBody(5, hardcover_utils::kReading, 1, 0)
          .find("{\"object\":{\"book_id\":5,\"status_id\":2,"
                "\"privacy_setting_id\":1}}") != std::string::npos);
  test::ExpectTrue(
      "insert read, still reading",
      hardcover_utils::InsertReadBody(9, 120, 0, "2026-10-01", "")
          .find("{\"id\":9,\"pages\":120,\"editionId\":null,"
                "\"startedAt\":\"2026-10-01\",\"finishedAt\":null}") !=
          std::string::npos);
  const std::string update =
      hardcover_utils::UpdateReadBody(11, 300, 77, "");
  test::ExpectTrue("update without finish date",
                   update.find("finished_at") == std::string::npos &&
                       update.find("{\"id\":11,\"pages\":300,\"editionId\":77}") !=
                           std::string::npos);
  const std::string finish =
      hardcover_utils::UpdateReadBody(11, 400, 77, "2026-10-01");
  test::ExpectTrue("update with finish date",
                   finish.find("finished_at: $finishedAt") != std::string::npos &&
                       finish.find("\"finishedAt\":\"2026-10-01\"") !=
                           std::string::npos);
}

} // namespace

int main() {
  TestCleanToken();
  TestIsbnFromFileName();
  TestLinksRoundTrip();
  TestUnlinkedEntries();
  TestMergeLinks();
  TestProgressPage();
  TestBodies();
  return 0;
}
