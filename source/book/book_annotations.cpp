/*
    3dslibris - book_annotations.cpp

    Book-side storage and lookup for highlights and notes. Anchors are
    re-resolved against the current pagination whenever the book is reflowed
    (layout revision, page count or page objects change).
*/

#include "book/book.h"

#include <time.h>

#include "book/annotation_store_utils.h"
#include "book/page.h"
#include "shared/debug_log.h"
#include "shared/path_constants.h"

namespace {

// Search this many pages either side of the remapped hint before scanning
// the whole book.
static const int kAnchorSearchWindow = 3;
static const size_t kMaxQuoteChars = 1000;
static const size_t kPrefixChars = 32;

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
  annotations_.clear();
  if (!SupportsAnnotations())
    return;
  if (!annotation_store_utils::LoadFile(AnnotationFilePath(), &annotations_)) {
    DBG_LOGF(GetStatusReporter(), "ANNOT load failed book=%s",
             GetFileName() ? GetFileName() : "");
    annotations_.clear();
  }
  InvalidateAnnotationSpans();
}

void Book::SaveAnnotations() {
  if (!annotation_store_utils::SaveFile(AnnotationFilePath(), annotations_)) {
    DBG_LOGF(GetStatusReporter(), "ANNOT save failed book=%s count=%u",
             GetFileName() ? GetFileName() : "",
             (unsigned)annotations_.size());
  }
}

const std::vector<Annotation> &Book::GetAnnotations() {
  EnsureAnnotationsLoaded();
  return annotations_;
}

const Annotation *Book::FindAnnotation(uint32_t id) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < annotations_.size(); i++)
    if (annotations_[i].id == id)
      return &annotations_[i];
  return NULL;
}

uint32_t Book::AddAnnotationFromPageRange(int page_index, int buf_begin,
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
  a.id = annotation_store_utils::NextId(annotations_);
  a.created = (uint32_t)time(NULL);
  a.page_hint = (uint16_t)page_index;
  a.page_count_hint = GetPageCount();
  a.note = note;
  annotations_.push_back(a);
  InvalidateAnnotationSpans();
  SaveAnnotations();
  return a.id;
}

bool Book::SetAnnotationNote(uint32_t id, const std::string &note) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < annotations_.size(); i++) {
    if (annotations_[i].id != id)
      continue;
    annotations_[i].note = note;
    SaveAnnotations();
    return true;
  }
  return false;
}

bool Book::RemoveAnnotation(uint32_t id) {
  EnsureAnnotationsLoaded();
  for (size_t i = 0; i < annotations_.size(); i++) {
    if (annotations_[i].id != id)
      continue;
    annotations_.erase(annotations_.begin() + i);
    InvalidateAnnotationSpans();
    SaveAnnotations();
    return true;
  }
  return false;
}

void Book::EnsureAnnotationSpans() {
  EnsureAnnotationsLoaded();
  const size_t page_count = pages.size();
  const Page *first_page = page_count ? pages[0] : NULL;
  if (annotation_spans_valid_ &&
      annotation_spans_revision_ == layout_revision &&
      annotation_spans_page_count_ == page_count &&
      annotation_spans_first_page_ == first_page)
    return;

  annotation_spans_.clear();
  // Pages may still be arriving from the reflow worker; resolve once the
  // open has finished.
  if (page_count == 0 || IsAsyncReflowOpenPending())
    return;

  for (size_t i = 0; i < annotations_.size(); i++) {
    Annotation &a = annotations_[i];
    AnnotationSpans resolved;
    resolved.id = a.id;
    if (annotation_text_utils::ResolveAnchor(
            a.quote, a.prefix, a.page_hint, a.page_count_hint,
            (int)page_count, PageBufferForAnchor, this, kAnchorSearchWindow,
            &resolved.spans) &&
        !resolved.spans.empty()) {
      // Remember where it is now so the next resolve starts in the right
      // place; persisted with the next save.
      a.page_hint = (uint16_t)resolved.spans[0].page;
      a.page_count_hint = (uint16_t)page_count;
    }
    annotation_spans_.push_back(resolved);
  }
  annotation_spans_valid_ = true;
  annotation_spans_revision_ = layout_revision;
  annotation_spans_page_count_ = page_count;
  annotation_spans_first_page_ = first_page;
}

int Book::GetAnnotationPage(uint32_t id) {
  EnsureAnnotationSpans();
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
  EnsureAnnotationSpans();
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

uint32_t Book::FindAnnotationAt(int page_index, int buf_index) {
  EnsureAnnotationSpans();
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

void Book::SetSelectionPreview(int buf_begin, int buf_end) {
  selection_preview_begin_ = buf_begin;
  selection_preview_end_ = buf_end;
}

void Book::ClearSelectionPreview() {
  selection_preview_begin_ = -1;
  selection_preview_end_ = -1;
}

bool Book::GetSelectionPreview(int *buf_begin, int *buf_end) const {
  if (selection_preview_begin_ < 0 ||
      selection_preview_end_ <= selection_preview_begin_)
    return false;
  if (buf_begin)
    *buf_begin = selection_preview_begin_;
  if (buf_end)
    *buf_end = selection_preview_end_;
  return true;
}
