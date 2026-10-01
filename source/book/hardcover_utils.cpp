/*
    3dslibris - hardcover_utils.cpp

    See include/book/hardcover_utils.h.
*/

#include "book/hardcover_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "book/readwise_api_utils.h"

namespace hardcover_utils {

namespace {

const char kLinksHeader[] = "3DSLIBRIS-HARDCOVER 1";

bool ValidIsbn13(const std::string &digits) {
  if (digits.size() != 13 ||
      (digits.compare(0, 3, "978") != 0 && digits.compare(0, 3, "979") != 0))
    return false;
  int sum = 0;
  for (int i = 0; i < 12; i++)
    sum += (digits[(size_t)i] - '0') * (i % 2 ? 3 : 1);
  return (10 - sum % 10) % 10 == digits[12] - '0';
}

std::string Int(int v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", v);
  return buf;
}

// An Int variable, or null for 0 (Hardcover treats null as "not set").
std::string IntOrNull(int v) { return v ? Int(v) : std::string("null"); }

std::string StringOrNull(const std::string &s) {
  return s.empty() ? std::string("null") : readwise_api_utils::JsonString(s);
}

std::string Body(const std::string &query, const std::string &variables) {
  return "{\"query\":" + readwise_api_utils::JsonString(query) +
         ",\"variables\":" + variables + "}";
}

std::string FieldSafe(const std::string &s) {
  std::string out = s;
  for (size_t i = 0; i < out.size(); i++)
    if (out[i] == '\t' || out[i] == '\n' || out[i] == '\r')
      out[i] = ' ';
  return out;
}

void SplitTabs(const std::string &line, std::vector<std::string> *out) {
  out->clear();
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    out->push_back(line.substr(start, tab == std::string::npos
                                          ? std::string::npos
                                          : tab - start));
    if (tab == std::string::npos)
      return;
    start = tab + 1;
  }
}

bool ParseInt(const std::string &s, int *out) {
  if (s.empty())
    return false;
  char *end = NULL;
  const long v = strtol(s.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  *out = (int)v;
  return true;
}

} // namespace

std::string CleanToken(const std::string &raw) {
  std::string token = readwise_api_utils::CleanToken(raw);
  if (token.size() > 6) {
    std::string head = token.substr(0, 6);
    for (size_t i = 0; i < head.size(); i++)
      head[i] = (char)tolower((unsigned char)head[i]);
    if (head == "bearer")
      token = token.substr(6);
  }
  return token;
}

std::string IsbnFromFileName(const std::string &file_name) {
  // Every run of 13 digits, optionally written with hyphens.
  for (size_t i = 0; i < file_name.size(); i++) {
    if (!isdigit((unsigned char)file_name[i]) ||
        (i > 0 && isdigit((unsigned char)file_name[i - 1])))
      continue;
    std::string digits;
    size_t j = i;
    while (j < file_name.size() && digits.size() < 14 &&
           (isdigit((unsigned char)file_name[j]) || file_name[j] == '-')) {
      if (file_name[j] != '-')
        digits.push_back(file_name[j]);
      j++;
    }
    if (ValidIsbn13(digits))
      return digits;
  }
  return std::string();
}

std::string SerializeLinks(const std::vector<Link> &links) {
  std::string out = kLinksHeader;
  out.push_back('\n');
  for (size_t i = 0; i < links.size(); i++) {
    const Link &l = links[i];
    out += FieldSafe(l.sync_id) + "\t" + Int(l.book_id) + "\t" +
           Int(l.edition_id) + "\t" + Int(l.pages) + "\t" +
           Int(l.last_sent_page) + "\t" + (l.finished ? "1" : "0") + "\t" +
           FieldSafe(l.title) + "\n";
  }
  return out;
}

std::vector<Link> ParseLinks(const std::string &data) {
  std::vector<Link> links;
  size_t pos = data.find('\n');
  if (pos == std::string::npos)
    return links;
  std::string header = data.substr(0, pos);
  if (!header.empty() && header[header.size() - 1] == '\r')
    header.erase(header.size() - 1);
  if (header != kLinksHeader)
    return links;
  pos++;
  std::vector<std::string> f;
  while (pos < data.size()) {
    size_t eol = data.find('\n', pos);
    if (eol == std::string::npos)
      eol = data.size();
    std::string line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line[line.size() - 1] == '\r')
      line.erase(line.size() - 1);
    SplitTabs(line, &f);
    Link l;
    int finished = 0;
    if (f.size() != 7 || f[0].empty() || !ParseInt(f[1], &l.book_id) ||
        l.book_id <= 0 || !ParseInt(f[2], &l.edition_id) ||
        !ParseInt(f[3], &l.pages) || !ParseInt(f[4], &l.last_sent_page) ||
        !ParseInt(f[5], &finished))
      continue;
    l.sync_id = f[0];
    l.finished = finished != 0;
    l.title = f[6];
    links.push_back(l);
  }
  return links;
}

int FindLink(const std::vector<Link> &links, const std::string &sync_id) {
  for (size_t i = 0; i < links.size(); i++)
    if (links[i].sync_id == sync_id)
      return (int)i;
  return -1;
}

int ProgressPage(int position, int page_count, int hardcover_pages) {
  if (page_count <= 0 || hardcover_pages <= 0 || position < 0)
    return 0;
  if (IsFinished(position, page_count))
    return hardcover_pages;
  const long long page =
      ((long long)(position + 1) * hardcover_pages + page_count / 2) /
      page_count;
  if (page < 1)
    return 1;
  return page > hardcover_pages ? hardcover_pages : (int)page;
}

bool IsFinished(int position, int page_count) {
  return page_count > 0 && position >= page_count - 1;
}

std::string DateString(uint32_t unix_time) {
  return readwise_api_utils::IsoTime(unix_time).substr(0, 10);
}

std::string MeBody() {
  return Body("{ me { id account_privacy_setting_id } }", "{}");
}

static const char kBookFields[] =
    "id title release_year pages cached_contributors";

std::string EditionByIsbnBody(const std::string &isbn13) {
  return Body(std::string("query ($isbn: String!) { editions(where: "
                          "{isbn_13: {_eq: $isbn}}, limit: 5) { id pages "
                          "title book { ") +
                  kBookFields + " } } }",
              "{\"isbn\":" + readwise_api_utils::JsonString(isbn13) + "}");
}

std::string SearchBody(const std::string &text, int per_page) {
  return Body("query ($query: String!, $perPage: Int!) { search(query: "
              "$query, per_page: $perPage, page: 1, query_type: \"Book\") "
              "{ ids } }",
              "{\"query\":" + readwise_api_utils::JsonString(text) +
                  ",\"perPage\":" + Int(per_page) + "}");
}

std::string BooksByIdsBody(const std::vector<int> &ids) {
  std::string list = "[";
  for (size_t i = 0; i < ids.size(); i++)
    list += (i ? "," : "") + Int(ids[i]);
  list += "]";
  return Body(std::string("query ($ids: [Int!]) { books(where: {id: {_in: "
                          "$ids}}) { ") +
                  kBookFields + " } }",
              "{\"ids\":" + list + "}");
}

std::string UserBooksBody(int book_id, int user_id) {
  return Body("query ($bookId: Int!, $userId: Int!) { user_books(where: "
              "{book_id: {_eq: $bookId}, user_id: {_eq: $userId}}) { id "
              "status_id user_book_reads(order_by: {id: asc}) { id "
              "finished_at } } }",
              "{\"bookId\":" + Int(book_id) + ",\"userId\":" + Int(user_id) +
                  "}");
}

std::string SetStatusBody(int book_id, int status, int privacy,
                          int edition_id) {
  std::string object = "{\"book_id\":" + Int(book_id) +
                       ",\"status_id\":" + Int(status);
  if (privacy)
    object += ",\"privacy_setting_id\":" + Int(privacy);
  if (edition_id)
    object += ",\"edition_id\":" + Int(edition_id);
  object += "}";
  return Body("mutation ($object: UserBookCreateInput!) { "
              "insert_user_book(object: $object) { error user_book { id "
              "status_id } } }",
              "{\"object\":" + object + "}");
}

std::string InsertReadBody(int user_book_id, int pages, int edition_id,
                           const std::string &started_date,
                           const std::string &finished_date) {
  return Body("mutation ($id: Int!, $pages: Int, $editionId: Int, "
              "$startedAt: date, $finishedAt: date) { "
              "insert_user_book_read(user_book_id: $id, user_book_read: { "
              "progress_pages: $pages, edition_id: $editionId, started_at: "
              "$startedAt, finished_at: $finishedAt }) { error "
              "user_book_read { id } } }",
              "{\"id\":" + Int(user_book_id) + ",\"pages\":" + Int(pages) +
                  ",\"editionId\":" + IntOrNull(edition_id) +
                  ",\"startedAt\":" + StringOrNull(started_date) +
                  ",\"finishedAt\":" + StringOrNull(finished_date) + "}");
}

std::string UpdateReadBody(int read_id, int pages, int edition_id,
                           const std::string &finished_date) {
  // finished_at is only sent when finishing, so an earlier date stays.
  const bool finishing = !finished_date.empty();
  return Body(std::string("mutation ($id: Int!, $pages: Int, $editionId: "
                          "Int") +
                  (finishing ? ", $finishedAt: date" : "") +
                  ") { update_user_book_read(id: $id, object: { "
                  "progress_pages: $pages, edition_id: $editionId" +
                  (finishing ? ", finished_at: $finishedAt" : "") +
                  " }) { error user_book_read { id } } }",
              "{\"id\":" + Int(read_id) + ",\"pages\":" + Int(pages) +
                  ",\"editionId\":" + IntOrNull(edition_id) +
                  (finishing ? ",\"finishedAt\":" +
                                   readwise_api_utils::JsonString(finished_date)
                             : std::string()) +
                  "}");
}

} // namespace hardcover_utils
