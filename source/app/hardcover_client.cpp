/*
    3dslibris - hardcover_client.cpp

    See include/app/hardcover_client.h.
*/

#include "app/hardcover_client.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

namespace hardcover_client {

namespace {

const char kApiUrl[] = "https://api.hardcover.app/v1/graphql";

// An integer field that may arrive as a number or a numeric string.
int IntOf(json_t *v) {
  if (json_is_integer(v))
    return (int)json_integer_value(v);
  if (json_is_real(v))
    return (int)json_real_value(v);
  if (json_is_string(v))
    return atoi(json_string_value(v));
  return 0;
}

std::string StringOf(json_t *v) {
  return json_is_string(v) ? json_string_value(v) : std::string();
}

// cached_contributors: [{"author": {"name": ...}, ...}, ...], possibly
// delivered as a JSON string.
std::string FirstAuthor(json_t *contributors) {
  json_t *owned = NULL;
  if (json_is_string(contributors)) {
    owned = json_loads(json_string_value(contributors), 0, NULL);
    contributors = owned;
  }
  std::string name;
  if (json_is_array(contributors) && json_array_size(contributors) > 0) {
    json_t *first = json_array_get(contributors, 0);
    json_t *author = json_object_get(first, "author");
    name = StringOf(json_object_get(author ? author : first, "name"));
  }
  if (owned)
    json_decref(owned);
  return name;
}

Candidate FromBook(json_t *book) {
  Candidate c;
  c.book_id = IntOf(json_object_get(book, "id"));
  c.title = StringOf(json_object_get(book, "title"));
  c.year = IntOf(json_object_get(book, "release_year"));
  c.pages = IntOf(json_object_get(book, "pages"));
  c.author = FirstAuthor(json_object_get(book, "cached_contributors"));
  return c;
}

bool HasBook(const std::vector<Candidate> &list, int book_id) {
  for (size_t i = 0; i < list.size(); i++)
    if (list[i].book_id == book_id)
      return true;
  return false;
}

std::string Today() {
  return hardcover_utils::DateString((uint32_t)time(NULL));
}

} // namespace

// A parsed reply; owns the JSON tree.
struct Client::Reply {
  json_t *root;
  json_t *data;
  Reply() : root(NULL), data(NULL) {}
  ~Reply() {
    if (root)
      json_decref(root);
  }
};

Client::Client(https_client::Session &session, const std::string &token)
    : session_(session), token_(token), auth_failed_(false), user_id_(0),
      privacy_(0) {}

bool Client::Post(const std::string &body, Reply *reply) {
  std::vector<std::string> headers;
  headers.push_back("authorization: Bearer " + token_);
  headers.push_back("Content-Type: application/json");
  https_client::Response response;
  if (!https_client::Request(session_, "POST", kApiUrl, headers, body,
                             &response)) {
    error_ = "Couldn't reach Hardcover: " + response.error;
    return false;
  }
  if (response.status == 401 || response.status == 403) {
    auth_failed_ = true;
    error_ = "Hardcover didn't accept the token.";
    return false;
  }
  json_error_t parse_error;
  reply->root = json_loads(response.body.c_str(), 0, &parse_error);
  if (!reply->root) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Unexpected reply from Hardcover (HTTP %ld).",
             response.status);
    error_ = buf;
    return false;
  }
  json_t *errors = json_object_get(reply->root, "errors");
  if (json_is_array(errors) && json_array_size(errors) > 0) {
    const std::string message =
        StringOf(json_object_get(json_array_get(errors, 0), "message"));
    if (message.find("jwt") != std::string::npos ||
        message.find("JWT") != std::string::npos ||
        message.find("token") != std::string::npos) {
      auth_failed_ = true;
      error_ = "Hardcover didn't accept the token.";
    } else {
      error_ = "Hardcover: " + message;
    }
    return false;
  }
  reply->data = json_object_get(reply->root, "data");
  if (!json_is_object(reply->data)) {
    error_ = "Unexpected reply from Hardcover.";
    return false;
  }
  return true;
}

bool Client::LoadMe() {
  if (user_id_)
    return true;
  Reply reply;
  if (!Post(hardcover_utils::MeBody(), &reply))
    return false;
  json_t *me = json_object_get(reply.data, "me");
  if (json_is_array(me))
    me = json_array_get(me, 0);
  user_id_ = IntOf(json_object_get(me, "id"));
  privacy_ = IntOf(json_object_get(me, "account_privacy_setting_id"));
  if (!user_id_) {
    auth_failed_ = true;
    error_ = "Hardcover didn't accept the token.";
    return false;
  }
  return true;
}

bool Client::FindCandidates(const std::string &isbn, const std::string &title,
                            const std::string &author, size_t max_results,
                            std::vector<Candidate> *out) {
  out->clear();
  if (!isbn.empty()) {
    Reply reply;
    if (!Post(hardcover_utils::EditionByIsbnBody(isbn), &reply))
      return false;
    json_t *editions = json_object_get(reply.data, "editions");
    for (size_t i = 0; i < json_array_size(editions); i++) {
      json_t *edition = json_array_get(editions, i);
      Candidate c = FromBook(json_object_get(edition, "book"));
      c.edition_id = IntOf(json_object_get(edition, "id"));
      const int pages = IntOf(json_object_get(edition, "pages"));
      if (pages > 0)
        c.pages = pages;
      if (c.book_id && !HasBook(*out, c.book_id))
        out->push_back(c);
    }
  }
  if (out->size() >= max_results || title.empty())
    return true;

  Reply search;
  const std::string text = author.empty() ? title : title + " " + author;
  if (!Post(hardcover_utils::SearchBody(text, (int)max_results), &search))
    return !out->empty();
  json_t *ids_json =
      json_object_get(json_object_get(search.data, "search"), "ids");
  std::vector<int> ids;
  for (size_t i = 0; i < json_array_size(ids_json); i++) {
    const int id = IntOf(json_array_get(ids_json, i));
    if (id > 0 && !HasBook(*out, id))
      ids.push_back(id);
  }
  if (ids.empty())
    return true;
  Reply books_reply;
  if (!Post(hardcover_utils::BooksByIdsBody(ids), &books_reply))
    return !out->empty();
  json_t *books = json_object_get(books_reply.data, "books");
  // Keep the search's order (best match first).
  for (size_t k = 0; k < ids.size() && out->size() < max_results; k++) {
    for (size_t i = 0; i < json_array_size(books); i++) {
      json_t *book = json_array_get(books, i);
      if (IntOf(json_object_get(book, "id")) == ids[k]) {
        out->push_back(FromBook(book));
        break;
      }
    }
  }
  return true;
}

bool Client::SendProgress(hardcover_utils::Link *link, int page,
                          bool finished) {
  if (!LoadMe())
    return false;
  const int want_status =
      finished ? hardcover_utils::kRead : hardcover_utils::kReading;

  int user_book_id = 0;
  int open_read_id = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    Reply reply;
    if (!Post(hardcover_utils::UserBooksBody(link->book_id, user_id_), &reply))
      return false;
    json_t *user_books = json_object_get(reply.data, "user_books");
    json_t *user_book = json_array_get(user_books, 0);
    const int status = IntOf(json_object_get(user_book, "status_id"));
    user_book_id = IntOf(json_object_get(user_book, "id"));
    json_t *reads = json_object_get(user_book, "user_book_reads");
    open_read_id = 0;
    for (size_t i = 0; i < json_array_size(reads); i++) {
      json_t *read = json_array_get(reads, i);
      if (json_is_null(json_object_get(read, "finished_at")) ||
          !json_object_get(read, "finished_at"))
        open_read_id = IntOf(json_object_get(read, "id")); // latest wins
    }
    if (user_book_id && status == want_status)
      break;
    if (attempt == 1)
      break; // status set; carry on with what we have
    // Not on the shelf yet, or a different status: set it, then look again.
    Reply set;
    if (!Post(hardcover_utils::SetStatusBody(link->book_id, want_status,
                                             privacy_, link->edition_id),
              &set))
      return false;
    const std::string err = StringOf(
        json_object_get(json_object_get(set.data, "insert_user_book"), "error"));
    if (!err.empty()) {
      error_ = "Hardcover: " + err;
      return false;
    }
  }
  if (!user_book_id) {
    error_ = "Hardcover didn't add the book to your shelf.";
    return false;
  }

  const std::string finished_date = finished ? Today() : std::string();
  Reply read_reply;
  const char *field = open_read_id ? "update_user_book_read"
                                   : "insert_user_book_read";
  const std::string body =
      open_read_id
          ? hardcover_utils::UpdateReadBody(open_read_id, page,
                                            link->edition_id, finished_date)
          : hardcover_utils::InsertReadBody(user_book_id, page,
                                            link->edition_id, Today(),
                                            finished_date);
  if (!Post(body, &read_reply))
    return false;
  const std::string err =
      StringOf(json_object_get(json_object_get(read_reply.data, field), "error"));
  if (!err.empty()) {
    error_ = "Hardcover: " + err;
    return false;
  }
  link->last_sent_page = page;
  link->finished = finished;
  return true;
}

} // namespace hardcover_client
