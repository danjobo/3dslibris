/*
    3dslibris - readwise_client.h

    Readwise API calls over the bundled HTTPS stack (see
    book/readwise_api_utils.h for what is sent and why): creating
    highlights, finding Readwise's id for one already there, updating its
    note, and setting its color tag. Blocking; device only.

    The list endpoints used to find ids are limited to 20 requests a
    minute; a 429 reply stops the upload with a "try again" message, and
    what was done so far is kept.
*/

#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "app/https_client.h"
#include "book/readwise_api_utils.h"

namespace readwise_client {

class Client {
public:
  Client(https_client::Session &session, const std::string &token);

  bool Create(const std::vector<readwise_api_utils::Highlight> &batch);
  // Readwise's id for a highlight already there (matched by book title and
  // author, then text). *id is 0 when it isn't found.
  bool FindHighlightId(const readwise_api_utils::Highlight &h, uint64_t *id);
  bool UpdateNote(uint64_t id, const std::string &note);
  // Leaves exactly one color tag on the highlight: this color's.
  bool SetColorTag(uint64_t id, uint8_t color);

  const std::string &error() const { return error_; }

private:
  struct Reply;
  bool Call(const char *method, const std::string &url,
            const std::string &body, Reply *reply);
  bool LoadBooks();

  https_client::Session &session_;
  std::string token_;
  std::string error_;
  bool books_loaded_;
  // Readwise books: (title, author) -> id.
  std::map<std::pair<std::string, std::string>, long long> books_;
  // Readwise book id -> (highlight id, text).
  std::map<long long, std::vector<std::pair<uint64_t, std::string> > >
      highlights_;
};

} // namespace readwise_client
