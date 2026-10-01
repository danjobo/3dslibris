/*
    3dslibris - hardcover_client.h

    Talks to Hardcover's GraphQL API over the bundled HTTPS stack: finding
    a book (by ISBN, then title and author) and recording reading progress
    (marking the book Reading or Read and updating the current read's
    page). Request bodies come from book/hardcover_utils. Blocking; device
    only.
*/

#pragma once

#include <string>
#include <vector>

#include "app/https_client.h"
#include "book/hardcover_utils.h"

namespace hardcover_client {

struct Candidate {
  int book_id;
  int edition_id; // 0 when found by title (Hardcover's default edition)
  int pages;
  int year;
  std::string title;
  std::string author;
  Candidate() : book_id(0), edition_id(0), pages(0), year(0) {}
};

class Client {
public:
  Client(https_client::Session &session, const std::string &token);

  // Up to max_results matches: ISBN editions first, then title search.
  bool FindCandidates(const std::string &isbn, const std::string &title,
                      const std::string &author, size_t max_results,
                      std::vector<Candidate> *out);
  // Sets the book to Reading (or Read when finished) and the current read
  // to page; updates link->last_sent_page / finished on success.
  bool SendProgress(hardcover_utils::Link *link, int page, bool finished);

  const std::string &error() const { return error_; }
  // The token was rejected (as opposed to a network or server problem).
  bool auth_failed() const { return auth_failed_; }

private:
  struct Reply;
  bool Post(const std::string &body, Reply *reply);
  bool LoadMe();

  https_client::Session &session_;
  std::string token_;
  std::string error_;
  bool auth_failed_;
  int user_id_;
  int privacy_;
};

} // namespace hardcover_client
