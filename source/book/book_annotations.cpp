/*
    3dslibris - book_annotations.cpp

    Book-side storage and lookup for per-book state: highlights and notes,
    anchored bookmarks and the reading position. Anchors are re-resolved
    against the current pagination whenever the book is reflowed (layout
    revision, page count or page objects change), so they survive font,
    spacing and orientation changes and can be synced between consoles.
*/

#include "book/book.h"

#include <algorithm>
#include <time.h>

#include "book/annotation_store_utils.h"
#include "book/page.h"
#include "shared/console_id.h"
#include "shared/debug_log.h"
#include "shared/path_constants.h"

namespace {

// Search this many pages either side of the remapped hint before scanning
// the whole book.
static const int kAnchorSearchWindow = 3;
static const size_t kMaxQuoteChars = 1000;
static const size_t kPrefixChars = 32;
// Page-start anchors (bookmarks, reading position).
static const size_t kPageAnchorChars = 48;

bool PageBufferForAnchor(void *ctx, int page, const uint32_t **buf,
                         int *len) {
  Book *book = static_cast<Book *>(ctx);
  Page *p = book ? book->GetPage(page) : NULL;
  if (!p || !p->GetBuffer() || p->GetLength() <= 0)
    return false;
  *buf = p->GetBuffer();
  *len = p->GetLength();
  return true;
}

uint32_t Now() { return (uint32_t)time(NULL); }

} // namespace

bool Book::SupportsAnnotations() const { return !IsFixedLayout(); }

std::string Book::AnnotationFilePath() {
  const char *folder = GetFolderName();
  const char *file = GetFileName();
  return paths::GetAnnotationsDir() + "/" +
         annotation_store_utils::BuildFileName(folder ? folder : "",
                                               file ? file : "");
}

void Book::EnsureAnnotationsLoaded() {
  if (annotations_loaded_)
    return;
  annotations_loaded_ = true;
  state_ = BookState();
  if (!annotation_store_utils::LoadFile(AnnotationFilePath(),
                                        console_id::Prefix(), &state_)) {
    DBG_LOGF(GetStatusReporter(), "ANNOT load failed book=%s",
             GetFileName() ? GetFileName() : "");
    state_ = BookState();
  }
  InvalidateAnnotationSpans();
}

void Book::SaveAnnotations() {
  if (!annotation_store_utils::SaveFile(AnnotationFilePath(), state_)) {
    DBG_LOGF(GetStatusReporter(), "ANNOT save failed book=%s count=%u",
             GetFileName() ? GetFileName() : "",
             (unsigned)state_.records.size());
  }
}

const std::vector<Annotation> &Book::GetAnnotations() {
  EnsureAnnotationsLoaded();
  return state_.records;
}

const Annotation *Book::FindAnnotation(uint64_t id) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < state_.records.size(); i++)
    if (state_.records[i].id == id && state_.records[i].IsLiveHighlight())
      return &state_.records[i];
  return NULL;
}

uint64_t Book::AddAnnotationFromPageRange(int page_index, int buf_begin,
                                          int buf_end,
                                          const std::string &note) {
  EnsureAnnotationsLoaded();
  if (!SupportsAnnotations())
    return 0;
  Page *page = GetPage(page_index);
  if (!page || !page->GetBuffer())
    return 0;
  Annotation a;
  if (!annotation_text_utils::BuildAnchorFromBufferRange(
          page->GetBuffer(), page->GetLength(), buf_begin, buf_end,
          kMaxQuoteChars, kPrefixChars, &a.quote, &a.prefix))
    return 0;
  a.id = annotation_store_utils::NextId(state_, console_id::Prefix());
  a.kind = Annotation::kHighlight;
  a.created = Now();
  a.modified = a.created;
  a.page_hint = (uint16_t)page_index;
  a.page_count_hint = GetPageCount();
  a.note = note;
  state_.records.push_back(a);
  InvalidateAnnotationSpans();
  SaveAnnotations();
  return a.id;
}

bool Book::SetAnnotationNote(uint64_t id, const std::string &note) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < state_.records.size(); i++) {
    Annotation &a = state_.records[i];
    if (a.id != id || !a.IsLiveHighlight())
      continue;
    a.note = note;
    a.modified = Now();
    SaveAnnotations();
    return true;
  }
  return false;
}

bool Book::RemoveAnnotation(uint64_t id) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < state_.records.size(); i++) {
    Annotation &a = state_.records[i];
    if (a.id != id || !a.IsLiveHighlight())
      continue;
    // Keep a tombstone so the deletion reaches other consoles; the quote is
    // no longer needed.
    a.deleted = true;
    a.modified = Now();
    a.quote.clear();
    a.prefix.clear();
    a.note.clear();
    InvalidateAnnotationSpans();
    SaveAnnotations();
    return true;
  }
  return false;
}

namespace {

bool ResolveHighlight(Book *book, Annotation *a, size_t page_count,
                      bool whole_book,
                      std::vector<annotation_text_utils::ResolvedSpan> *out) {
  if (!annotation_text_utils::ResolveAnchor(
          a->quote, a->prefix, a->page_hint, a->page_count_hint,
          (int)page_count, PageBufferForAnchor, book, kAnchorSearchWindow,
          out, whole_book) ||
      out->empty())
    return false;
  // Remember where it is now so the next resolve starts in the right place;
  // persisted with the next save.
  a->page_hint = (uint16_t)(*out)[0].page;
  a->page_count_hint = (uint16_t)page_count;
  return true;
}

} // namespace

void Book::EnsureAnnotationSpans(bool allow_full_scan) {
  EnsureAnnotationsLoaded();
  if (state_.records.empty()) {
    annotation_spans_.clear();
    return;
  }
  const size_t page_count = pages.size();
  const Page *first_page = page_count ? pages[0] : NULL;
  const bool cache_matches =
      annotation_spans_valid_ &&
      annotation_spans_revision_ == layout_revision &&
      annotation_spans_page_count_ == page_count &&
      annotation_spans_first_page_ == first_page;

  if (!cache_matches) {
    annotation_spans_.clear();
    annotation_full_scan_done_ = false;
    // Pages may still be arriving from the reflow worker; resolve once the
    // open has finished.
    if (page_count == 0 || IsAsyncReflowOpenPending() ||
        !SupportsAnnotations())
      return;
    for (size_t i = 0; i < state_.records.size(); i++) {
      if (!state_.records[i].IsLiveHighlight())
        continue;
      AnnotationSpans resolved;
      resolved.id = state_.records[i].id;
      ResolveHighlight(this, &state_.records[i], page_count, false,
                       &resolved.spans);
      annotation_spans_.push_back(resolved);
    }
    annotation_spans_valid_ = true;
    annotation_spans_revision_ = layout_revision;
    annotation_spans_page_count_ = page_count;
    annotation_spans_first_page_ = first_page;
  }

  if (allow_full_scan && !annotation_full_scan_done_ &&
      annotation_spans_valid_) {
    annotation_full_scan_done_ = true;
    for (size_t i = 0; i < annotation_spans_.size(); i++) {
      if (!annotation_spans_[i].spans.empty())
        continue;
      for (size_t r = 0; r < state_.records.size(); r++) {
        if (state_.records[r].id != annotation_spans_[i].id)
          continue;
        ResolveHighlight(this, &state_.records[r], page_count, true,
                         &annotation_spans_[i].spans);
        break;
      }
    }
  }
}

int Book::GetAnnotationPage(uint64_t id) {
  EnsureAnnotationSpans(true);
  for (size_t i = 0; i < annotation_spans_.size(); i++) {
    if (annotation_spans_[i].id == id)
      return annotation_spans_[i].spans.empty()
                 ? -1
                 : annotation_spans_[i].spans[0].page;
  }
  return -1;
}

int Book::GetPageIndex(const Page *page) {
  if (!page)
    return -1;
  if (position >= 0 && position < (int)pages.size() && pages[position] == page)
    return position;
  for (size_t i = 0; i < pages.size(); i++)
    if (pages[i] == page)
      return (int)i;
  return -1;
}

void Book::CollectHighlightRanges(const Page *page,
                                  std::vector<HighlightRange> *out) {
  if (!out)
    return;
  out->clear();
  if (!SupportsAnnotations())
    return;
  const int page_index = GetPageIndex(page);
  if (page_index < 0)
    return;
  EnsureAnnotationSpans(false);
  for (size_t i = 0; i < annotation_spans_.size(); i++) {
    const AnnotationSpans &entry = annotation_spans_[i];
    for (size_t s = 0; s < entry.spans.size(); s++) {
      if (entry.spans[s].page != page_index)
        continue;
      HighlightRange range;
      range.buf_begin = entry.spans[s].buf_begin;
      range.buf_end = entry.spans[s].buf_end;
      range.annotation_id = entry.id;
      out->push_back(range);
    }
  }
}

uint64_t Book::FindAnnotationAt(int page_index, int buf_index) {
  EnsureAnnotationSpans(false);
  for (size_t i = 0; i < annotation_spans_.size(); i++) {
    const AnnotationSpans &entry = annotation_spans_[i];
    for (size_t s = 0; s < entry.spans.size(); s++) {
      const annotation_text_utils::ResolvedSpan &span = entry.spans[s];
      if (span.page == page_index && buf_index >= span.buf_begin &&
          buf_index < span.buf_end)
        return entry.id;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Bookmarks and reading position
// ---------------------------------------------------------------------------

void Book::BuildPageStartAnchor(int page_index, std::string *quote,
                                std::string *prefix) {
  quote->clear();
  prefix->clear();
  if (!SupportsAnnotations())
    return;
  Page *page = GetPage(page_index);
  if (!page || !page->GetBuffer() || page->GetLength() <= 0)
    return;
  std::string unused_prefix;
  if (!annotation_text_utils::BuildAnchorFromBufferRange(
          page->GetBuffer(), page->GetLength(), 0, page->GetLength(),
          kPageAnchorChars, 0, quote, &unused_prefix))
    return;
  // Context from the end of the previous page tells repeated openings
  // (e.g. "CHAPTER") apart.
  Page *prev = page_index > 0 ? GetPage(page_index - 1) : NULL;
  if (prev && prev->GetBuffer() && prev->GetLength() > 0) {
    annotation_text_utils::VisibleText raw, norm;
    annotation_text_utils::ExtractVisibleText(prev->GetBuffer(),
                                              prev->GetLength(), &raw);
    annotation_text_utils::NormalizeVisibleText(raw, &norm);
    size_t end = norm.chars.size();
    while (end > 0 && norm.chars[end - 1] == ' ')
      end--;
    size_t begin = end > kPrefixChars ? end - kPrefixChars : 0;
    while (begin < end && norm.chars[begin] == ' ')
      begin++;
    *prefix = annotation_text_utils::CodepointsToUtf8(norm.chars, begin, end);
  }
}

int Book::ResolvePageStartAnchor(const std::string &quote,
                                 const std::string &prefix, int page_hint,
                                 int page_count_hint) {
  const int page_count = (int)GetPageCount();
  if (page_count <= 0)
    return -1;
  if (!quote.empty() && SupportsAnnotations() && !IsAsyncReflowOpenPending()) {
    std::vector<annotation_text_utils::ResolvedSpan> spans;
    if (annotation_text_utils::ResolveAnchor(
            quote, prefix, page_hint, page_count_hint, page_count,
            PageBufferForAnchor, this, kAnchorSearchWindow, &spans, true) &&
        !spans.empty())
      return spans[0].page;
  }
  return annotation_text_utils::RemapPageHint(page_hint, page_count_hint,
                                              page_count);
}

void Book::OnBookmarkToggled(int page_index, bool added) {
  EnsureAnnotationsLoaded();
  const uint32_t now = Now();
  if (added) {
    Annotation b;
    b.id = annotation_store_utils::NextId(state_, console_id::Prefix());
    b.kind = Annotation::kBookmark;
    b.created = now;
    b.modified = now;
    b.page_hint = (uint16_t)page_index;
    b.page_count_hint = GetPageCount();
    BuildPageStartAnchor(page_index, &b.quote, &b.prefix);
    state_.records.push_back(b);
  } else {
    for (size_t i = 0; i < state_.records.size(); i++) {
      Annotation &b = state_.records[i];
      if (!b.IsLiveBookmark())
        continue;
      // Hints are refreshed after every layout, so they're usually current.
      const int page = (b.page_count_hint == GetPageCount())
                           ? (int)b.page_hint
                           : ResolvePageStartAnchor(b.quote, b.prefix,
                                                    b.page_hint,
                                                    b.page_count_hint);
      if (page != page_index)
        continue;
      b.deleted = true;
      b.modified = now;
    }
  }
  SaveAnnotations();
}

void Book::NoteReadingActivity(uint32_t now) {
  if (now)
    pending_last_read_ = now;
}

void Book::SaveReadingProgress() {
  if (GetPageCount() == 0)
    return;
  EnsureAnnotationsLoaded();
  const bool moved = position != progress_saved_position_;
  const bool read = pending_last_read_ != 0 &&
                    pending_last_read_ != state_.progress.last_read;
  if (!moved && !read && state_.has_progress)
    return;
  ReadingProgress p = state_.progress;
  if (read)
    p.last_read = pending_last_read_;
  p.page_hint = (uint16_t)position;
  p.page_count_hint = GetPageCount();
  BuildPageStartAnchor(position, &p.quote, &p.prefix);
  state_.progress = p;
  state_.has_progress = true;
  progress_saved_position_ = position;
  SaveAnnotations();
}

bool Book::ApplyAnchoredStateAfterLayout() {
  EnsureAnnotationsLoaded();
  if (GetPageCount() == 0)
    return false;

  // Bookmarks: the anchored records are the source of truth. The first time
  // a book is opened with this version, its page-number bookmarks become
  // records.
  bool has_bookmark_records = false;
  for (size_t i = 0; i < state_.records.size(); i++)
    if (state_.records[i].kind == Annotation::kBookmark)
      has_bookmark_records = true;

  std::list<u16> &pages_marked = GetBookmarks();
  if (!has_bookmark_records) {
    if (!pages_marked.empty()) {
      const uint32_t now = Now();
      for (std::list<u16>::const_iterator it = pages_marked.begin();
           it != pages_marked.end(); ++it) {
        Annotation b;
        b.id = annotation_store_utils::NextId(state_, console_id::Prefix());
        b.kind = Annotation::kBookmark;
        b.created = now;
        b.modified = now;
        b.page_hint = *it;
        b.page_count_hint = GetPageCount();
        BuildPageStartAnchor((int)*it, &b.quote, &b.prefix);
        state_.records.push_back(b);
      }
      SaveAnnotations();
    }
  } else {
    std::vector<u16> resolved;
    for (size_t i = 0; i < state_.records.size(); i++) {
      Annotation &b = state_.records[i];
      if (!b.IsLiveBookmark())
        continue;
      const int page = ResolvePageStartAnchor(b.quote, b.prefix, b.page_hint,
                                              b.page_count_hint);
      if (page < 0)
        continue;
      b.page_hint = (uint16_t)page;
      b.page_count_hint = GetPageCount();
      resolved.push_back((u16)page);
    }
    std::sort(resolved.begin(), resolved.end());
    resolved.erase(std::unique(resolved.begin(), resolved.end()),
                   resolved.end());
    pages_marked.assign(resolved.begin(), resolved.end());
  }

  // Reading position: re-find the saved page start by its text.
  bool moved = false;
  if (state_.has_progress) {
    const ReadingProgress &p = state_.progress;
    const int page = ResolvePageStartAnchor(p.quote, p.prefix, p.page_hint,
                                            p.page_count_hint);
    if (page >= 0 && page != position) {
      SetPosition(page);
      moved = true;
    }
  }
  progress_saved_position_ = position;
  return moved;
}
