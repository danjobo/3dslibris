/*
    3dslibris - hardcover_utils.h

    Pure helpers for tracking reading progress on Hardcover
    (https://api.hardcover.app/v1/graphql, "authorization: Bearer <token>"):
    the GraphQL request bodies, the saved links between books here and
    Hardcover books (hardcover-links.txt), the page number to report, and
    an ISBN read from the file name. Host-tested.

    Requests and fields follow Hardcover's public API as used by the
    KOReader plugin (github.com/Billiam/hardcoverapp.koplugin):
    statuses 1 = want to read, 2 = reading, 3 = read.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace hardcover_utils {

enum Status { kWantToRead = 1, kReading = 2, kRead = 3 };

// The token as pasted: whitespace and a leading "Bearer" are dropped.
std::string CleanToken(const std::string &raw);

// An ISBN-13 (978/979, valid check digit) found in a file name, e.g.
// Anna's Archive's "... -- isbn13 9780307266392 -- ...". Empty if none.
std::string IsbnFromFileName(const std::string &file_name);

// A book here linked to a Hardcover book (and edition, if known).
struct Link {
  std::string sync_id; // file name + size (see sync_merge::MakeSyncBookId)
  int book_id;
  int edition_id;      // 0 = none
  int pages;           // Hardcover's page count for the book/edition
  std::string title;   // for display
  int last_sent_page;  // 0 = nothing sent yet
  bool finished;       // already marked Read

  Link()
      : book_id(0), edition_id(0), pages(0), last_sent_page(0),
        finished(false) {}
};

std::string SerializeLinks(const std::vector<Link> &links);
std::vector<Link> ParseLinks(const std::string &data);
// Index of the link for sync_id, or -1.
int FindLink(const std::vector<Link> &links, const std::string &sync_id);

// The page to report: how far through the book this console is, scaled
// to Hardcover's page count (1..pages). 0 if unknown.
int ProgressPage(int position, int page_count, int hardcover_pages);
// On the last page here.
bool IsFinished(int position, int page_count);

// "YYYY-MM-DD" (console clock, treated as UTC).
std::string DateString(uint32_t unix_time);

// GraphQL request bodies: {"query": ..., "variables": {...}}.
std::string MeBody();
std::string EditionByIsbnBody(const std::string &isbn13);
std::string SearchBody(const std::string &text, int per_page);
std::string BooksByIdsBody(const std::vector<int> &ids);
std::string UserBooksBody(int book_id, int user_id);
// status_id and privacy (0 = account default, left out); edition 0 = none.
std::string SetStatusBody(int book_id, int status, int privacy,
                          int edition_id);
// A new read; finished_date empty while still reading.
std::string InsertReadBody(int user_book_id, int pages, int edition_id,
                           const std::string &started_date,
                           const std::string &finished_date);
std::string UpdateReadBody(int read_id, int pages, int edition_id,
                           const std::string &finished_date);

} // namespace hardcover_utils
