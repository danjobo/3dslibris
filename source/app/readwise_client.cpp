/*
    3dslibris - readwise_client.cpp

    See include/app/readwise_client.h.
*/

#include "app/readwise_client.h"

#include <jansson.h>
#include <stdio.h>

#include "book/highlight_color_utils.h"

namespace readwise_client {

namespace {

const char kApi[] = "https://readwise.io/api/v2/";
// Pages followed for one list (1000 items each).
const int kMaxPages = 20;

long long IdOf(json_t *v) {
  if (json_is_integer(v))
    return (long long)json_integer_value(v);
  if (json_is_string(v))
    return atoll(json_string_value(v));
  return 0;
}

std::string StringOf(json_t *v) {
  return json_is_string(v) ? json_string_value(v) : std::string();
}

std::string Id(unsigned long long id) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", id);
  return buf;
}

// Paged lists come as {"results": [...], "next": url}; some as a bare array.
json_t *Results(json_t *root) {
  return json_is_array(root) ? root : json_object_get(root, "results");
}

std::string NextUrl(json_t *root) {
  return json_is_object(root) ? StringOf(json_object_get(root, "next"))
                              : std::string();
}

bool IsColorTag(const std::string &name) {
  for (int c = 0; c < highlight_color_utils::kCount; c++)
    if (name == highlight_color_utils::Name((uint8_t)c))
      return true;
  return false;
}

} // namespace

struct Client::Reply {
  json_t *root;
  Reply() : root(NULL) {}
  ~Reply() {
    if (root)
      json_decref(root);
  }
};

Client::Client(https_client::Session &session, const std::string &token)
    : session_(session), token_(token), books_loaded_(false) {}

bool Client::Call(const char *method, const std::string &url,
                  const std::string &body, Reply *reply) {
  std::vector<std::string> headers;
  headers.push_back("Authorization: Token " + token_);
  if (!body.empty())
    headers.push_back("Content-Type: application/json");
  https_client::Response response;
  if (!https_client::Request(session_, method, url, headers, body, &response,
                             60)) {
    error_ = "Couldn't reach Readwise: " + response.error;
    return false;
  }
  if (response.status == 401 || response.status == 403) {
    error_ = "Readwise didn't accept the token.";
    return false;
  }
  if (response.status == 429) {
    error_ = "Readwise asked to slow down. Try again in a minute; what was "
             "sent is kept.";
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Readwise answered HTTP %ld.",
             response.status);
    error_ = buf;
    return false;
  }
  if (reply && !response.body.empty())
    reply->root = json_loads(response.body.c_str(), 0, NULL);
  return true;
}

bool Client::Create(const std::vector<readwise_api_utils::Highlight> &batch) {
  return Call("POST", std::string(kApi) + "highlights/",
              readwise_api_utils::BuildHighlightsJson(batch), NULL);
}

bool Client::LoadBooks() {
  if (books_loaded_)
    return true;
  std::string url = std::string(kApi) + "books/?category=books&page_size=1000";
  for (int page = 0; page < kMaxPages && !url.empty(); page++) {
    Reply reply;
    if (!Call("GET", url, "", &reply))
      return false;
    json_t *results = Results(reply.root);
    for (size_t i = 0; i < json_array_size(results); i++) {
      json_t *book = json_array_get(results, i);
      books_[std::make_pair(StringOf(json_object_get(book, "title")),
                            StringOf(json_object_get(book, "author")))] =
          IdOf(json_object_get(book, "id"));
    }
    url = NextUrl(reply.root);
  }
  books_loaded_ = true;
  return true;
}

bool Client::FindHighlightId(const readwise_api_utils::Highlight &h,
                             uint64_t *id) {
  *id = 0;
  if (!LoadBooks())
    return false;
  // Same title and author; failing that, the same title.
  long long book_id = 0;
  std::map<std::pair<std::string, std::string>, long long>::const_iterator it =
      books_.find(std::make_pair(h.title, h.author));
  if (it != books_.end()) {
    book_id = it->second;
  } else {
    for (it = books_.begin(); it != books_.end() && !book_id; ++it)
      if (it->first.first == h.title)
        book_id = it->second;
  }
  if (!book_id)
    return true;

  if (highlights_.find(book_id) == highlights_.end()) {
    std::vector<std::pair<uint64_t, std::string> > &list = highlights_[book_id];
    std::string url = std::string(kApi) + "highlights/?page_size=1000&book_id=" +
                      Id((unsigned long long)book_id);
    for (int page = 0; page < kMaxPages && !url.empty(); page++) {
      Reply reply;
      if (!Call("GET", url, "", &reply)) {
        highlights_.erase(book_id);
        return false;
      }
      json_t *results = Results(reply.root);
      for (size_t i = 0; i < json_array_size(results); i++) {
        json_t *item = json_array_get(results, i);
        list.push_back(std::make_pair(
            (uint64_t)IdOf(json_object_get(item, "id")),
            StringOf(json_object_get(item, "text"))));
      }
      url = NextUrl(reply.root);
    }
  }
  const std::vector<std::pair<uint64_t, std::string> > &list =
      highlights_[book_id];
  for (size_t i = 0; i < list.size(); i++) {
    if (readwise_api_utils::SameText(list[i].second, h.text)) {
      *id = list[i].first;
      break;
    }
  }
  return true;
}

bool Client::UpdateNote(uint64_t id, const std::string &note) {
  return Call("PATCH",
              std::string(kApi) + "highlights/" +
                  Id((unsigned long long)id) + "/",
              readwise_api_utils::PatchNoteJson(note), NULL);
}

bool Client::SetColorTag(uint64_t id, uint8_t color) {
  const std::string base = std::string(kApi) + "highlights/" +
                           Id((unsigned long long)id) + "/tags/";
  const std::string want = readwise_api_utils::ColorTag(color);
  Reply reply;
  if (!Call("GET", base, "", &reply))
    return false;
  bool have = false;
  json_t *results = Results(reply.root);
  for (size_t i = 0; i < json_array_size(results); i++) {
    json_t *tag = json_array_get(results, i);
    const std::string name = StringOf(json_object_get(tag, "name"));
    if (name == want) {
      have = true;
    } else if (IsColorTag(name)) {
      // Another color: this highlight's color changed.
      if (!Call("DELETE",
                base + Id((unsigned long long)IdOf(json_object_get(tag, "id"))) +
                    "/",
                "", NULL))
        return false;
    }
  }
  return have || Call("POST", base, readwise_api_utils::TagJson(want), NULL);
}

} // namespace readwise_client
