#include "book/readwise_export.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "book/book.h"
#include "book/readwise_csv_utils.h"
#include "shared/path_constants.h"

namespace readwise_export {

namespace {

Result WriteRows(const std::vector<readwise_csv_utils::Row> &rows,
                 int books) {
  Result result;
  result.books = books;
  result.highlights = (int)rows.size();
  if (rows.empty())
    return result;

  mkdir(paths::GetExportsDir().c_str(), 0777);
  result.path = paths::GetExportsDir() + "/" +
                readwise_csv_utils::BuildFileName((uint32_t)time(NULL));
  FILE *fp = fopen(result.path.c_str(), "wb");
  if (!fp)
    return result;
  const std::string csv = readwise_csv_utils::BuildCsv(rows);
  const bool wrote = fwrite(csv.data(), 1, csv.size(), fp) == csv.size();
  result.ok = (fclose(fp) == 0) && wrote;
  if (!result.ok)
    remove(result.path.c_str());
  return result;
}

} // namespace

Result ExportHighlights(
    const std::vector<readwise_api_utils::Highlight> &highlights, int books) {
  std::vector<readwise_csv_utils::Row> rows;
  for (size_t i = 0; i < highlights.size(); i++) {
    const readwise_api_utils::Highlight &h = highlights[i];
    readwise_csv_utils::Row row;
    row.highlight = h.text;
    row.title = h.title;
    row.author = h.author;
    row.note = readwise_api_utils::NoteWithTag(h.color, h.note);
    row.location = h.location;
    row.date = h.highlighted_at;
    rows.push_back(row);
  }
  return WriteRows(rows, books);
}

Result ExportBooks(const std::vector<Book *> &books) {
  Result result;
  std::vector<readwise_csv_utils::Row> rows;

  for (size_t b = 0; b < books.size(); b++) {
    Book *book = books[b];
    if (!book || book->IsBrowserFolder() || !book->SupportsAnnotations())
      continue;
    const std::vector<Annotation> &annotations = book->GetAnnotations();
    bool has_highlights = false;
    for (size_t i = 0; i < annotations.size() && !has_highlights; i++)
      has_highlights = annotations[i].IsLiveHighlight();
    if (!has_highlights)
      continue;

    const char *raw_title = book->GetTitle();
    std::string title = raw_title ? raw_title : "";
    if (title.empty() && book->GetFileName())
      title = book->GetFileName();
    const std::string &author = book->GetAuthor();

    for (size_t i = 0; i < annotations.size(); i++) {
      const Annotation &a = annotations[i];
      if (!a.IsLiveHighlight())
        continue;
      readwise_csv_utils::Row row;
      row.highlight = a.quote;
      row.title = title;
      row.author = author;
      row.note = readwise_api_utils::NoteWithTag(a.color, a.note);
      // Current page when the book is open and the text is found; otherwise
      // the page where the highlight was last seen.
      const int page = book->GetAnnotationPage(a.id);
      row.location = (page >= 0 ? page : (int)a.page_hint) + 1;
      row.date = a.created;
      rows.push_back(row);
    }
    result.books++;
  }
  return WriteRows(rows, result.books);
}

} // namespace readwise_export
