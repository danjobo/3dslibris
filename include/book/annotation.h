/*
    3dslibris - annotation.h

    Per-book reading state that survives re-layout and can be synced between
    consoles: highlights (with optional notes), bookmarks, saved character
    names and the reading position.

    Records are anchored by quoted text (plus a little preceding context),
    not by page index or buffer offset: both change whenever font size,
    spacing or orientation reflow the book. page_hint/page_count_hint only
    narrow the search when an anchor is re-resolved. Fixed-layout books have
    no text to quote; their records keep an empty quote and use page_hint
    directly (their pagination is the same on every console).
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

struct Annotation {
  // kCharacter: a name the reader is tracking; quote holds the name and
  // nothing else is used (it isn't anchored to a page).
  enum Kind : char { kHighlight = 'H', kBookmark = 'B', kCharacter = 'C' };

  // Unique across consoles: high 32 bits identify the console that created
  // the record, low 32 bits count records it created for this book.
  uint64_t id;
  Kind kind;
  uint32_t created;          // Unix timestamp; 0 if unknown.
  uint32_t modified;         // Last change (edit or deletion).
  bool deleted;              // Tombstone: kept so deletions sync.
  uint16_t page_hint;        // Page index when created or last resolved.
  uint16_t page_count_hint;  // Book page count at that time.
  std::string quote;         // Normalized anchor text (UTF-8).
  std::string prefix;        // Normalized text just before the quote.
  std::string note;          // Highlights only; optional.
  uint8_t color;             // Highlights only; highlight_color_utils::Color.
  // Readwise upload state (highlights only), synced so every console
  // knows: the `modified` value last sent (0 = never) and Readwise's id for
  // the highlight (0 = not looked up yet). Changing these doesn't change
  // `modified`.
  uint32_t readwise_uploaded;
  uint64_t readwise_id;

  Annotation()
      : id(0), kind(kHighlight), created(0), modified(0), deleted(false),
        page_hint(0), page_count_hint(0), color(0), readwise_uploaded(0),
        readwise_id(0) {}

  bool IsLiveHighlight() const { return kind == kHighlight && !deleted; }
  bool IsLiveBookmark() const { return kind == kBookmark && !deleted; }
  bool IsLiveCharacter() const { return kind == kCharacter && !deleted; }
};

// Where the reader is in the book, and when they last moved there.
struct ReadingProgress {
  uint32_t last_read;        // Unix timestamp of the last page turn; 0 = none
  uint16_t page_hint;
  uint16_t page_count_hint;
  std::string quote;         // Start of the page's text (anchor).
  std::string prefix;

  ReadingProgress() : last_read(0), page_hint(0), page_count_hint(0) {}
};

struct BookState {
  std::vector<Annotation> records; // highlights and bookmarks, incl. deleted
  bool has_progress;
  ReadingProgress progress;

  BookState() : has_progress(false) {}
  bool Empty() const { return records.empty() && !has_progress; }
};
