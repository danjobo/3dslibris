/*
    3dslibris - library_files.cpp

    See include/app/library_files.h.
*/

#include "app/library_files.h"

#include "app/app.h"
#include "book/annotation_store_utils.h"
#include "book/book.h"
#include "shared/console_id.h"
#include "shared/app_flow_utils.h"
#include "shared/path_constants.h"
#include "shared/utf8_utils.h"

namespace library_files {

namespace {

std::string NormalizeName(const std::string &raw) {
  return utf8_utils::NormalizeFsFilenameForIo(raw);
}

// The Book already loaded for a file (library view or open book), if any.
Book *LoadedBook(App &app, const sync_book_files::LocalBook &file) {
  Book *current = app.GetCurrentBook();
  for (size_t i = 0; i <= app.books.size(); i++) {
    Book *book = i < app.books.size() ? app.books[i] : current;
    if (book && !book->IsBrowserFolder() && book->GetFileName() &&
        book->GetFolderName() && file.file_name == book->GetFileName() &&
        file.folder == book->GetFolderName())
      return book;
  }
  return NULL;
}

std::string TitleFor(Book *book, const std::string &file_name) {
  const char *title = book->GetTitle();
  std::string out = title ? title : "";
  // Without cached metadata the title is the file name; drop its extension.
  if (out.empty() || out == file_name) {
    out = file_name;
    const size_t dot = out.rfind('.');
    if (dot != std::string::npos && dot > 0)
      out = out.substr(0, dot);
  }
  return out;
}

void AddHighlights(Book *book, bool loaded,
                   const sync_book_files::LocalBook &file, HighlightSet *set) {
  if (!book->SupportsAnnotations())
    return;
  const std::vector<Annotation> &records = book->GetAnnotations();
  bool any = false;
  for (size_t i = 0; i < records.size(); i++) {
    const Annotation &a = records[i];
    if (!a.IsLiveHighlight())
      continue;
    readwise_api_utils::Highlight h;
    h.id = a.id;
    h.modified = a.modified;
    h.text = a.quote;
    h.title = TitleFor(book, file.file_name);
    h.author = book->GetAuthor();
    h.note = a.note;
    // The current page when the book is open and the text is found;
    // otherwise where the highlight was last seen.
    const int page = loaded ? book->GetAnnotationPage(a.id) : -1;
    h.location = (page >= 0 ? page : (int)a.page_hint) + 1;
    h.highlighted_at = a.created;
    h.color = a.color;
    h.readwise_uploaded = a.readwise_uploaded;
    h.readwise_id = a.readwise_id;
    h.folder = file.folder;
    h.file_name = file.file_name;
    set->highlights.push_back(h);
    any = true;
  }
  if (any)
    set->books++;
}

} // namespace

bool IsLibraryBook(const char *name) {
  return app_flow_utils::ShouldIndexBookFilename(name) &&
         app_flow_utils::DetectBookFormat(name) != FORMAT_UNDEF;
}

std::vector<sync_book_files::LocalBook> ScanAll(const App &app) {
  std::vector<std::string> roots;
  roots.push_back(app.bookdir);
  if (app.bookdir != paths::kRomfsBookDir)
    roots.push_back(paths::kRomfsBookDir);
  return sync_book_files::ScanLibrary(roots, &IsLibraryBook, &NormalizeName);
}

HighlightSet CollectHighlights(App &app) {
  HighlightSet set;
  const std::vector<sync_book_files::LocalBook> files = ScanAll(app);
  for (size_t f = 0; f < files.size(); f++) {
    Book *loaded = LoadedBook(app, files[f]);
    Book *book = loaded ? loaded
                        : app.CreateDetachedBook(files[f].folder,
                                                 files[f].file_name);
    if (!book)
      continue;
    AddHighlights(book, loaded != NULL, files[f], &set);
    if (!loaded)
      delete book;
  }
  return set;
}

HighlightSet CollectBookHighlights(Book *book) {
  HighlightSet set;
  if (!book || book->IsBrowserFolder() || !book->GetFileName() ||
      !book->GetFolderName())
    return set;
  sync_book_files::LocalBook file;
  file.folder = book->GetFolderName();
  file.file_name = book->GetFileName();
  AddHighlights(book, true, file, &set);
  return set;
}

int SaveUploadState(App &app,
                    const std::vector<readwise_api_utils::Highlight> &done) {
  int saved = 0;
  std::vector<bool> handled(done.size(), false);
  for (size_t i = 0; i < done.size(); i++) {
    if (handled[i])
      continue;
    // Everything for this book at once.
    sync_book_files::LocalBook file;
    file.folder = done[i].folder;
    file.file_name = done[i].file_name;
    Book *loaded = LoadedBook(app, file);
    const std::string path =
        paths::GetAnnotationsDir() + "/" +
        annotation_store_utils::BuildFileName(file.folder, file.file_name);
    BookState state;
    const bool have_file =
        loaded || annotation_store_utils::LoadFile(path, console_id::Prefix(),
                                                   &state);
    for (size_t j = i; j < done.size(); j++) {
      if (handled[j] || done[j].folder != file.folder ||
          done[j].file_name != file.file_name)
        continue;
      handled[j] = true;
      if (loaded) {
        if (loaded->SetReadwiseState(done[j].id, done[j].readwise_uploaded,
                                     done[j].readwise_id))
          saved++;
        continue;
      }
      for (size_t r = 0; have_file && r < state.records.size(); r++) {
        if (state.records[r].id != done[j].id)
          continue;
        state.records[r].readwise_uploaded = done[j].readwise_uploaded;
        if (done[j].readwise_id)
          state.records[r].readwise_id = done[j].readwise_id;
        saved++;
      }
    }
    if (!loaded && have_file)
      annotation_store_utils::SaveFile(path, state);
  }
  return saved;
}

} // namespace library_files
