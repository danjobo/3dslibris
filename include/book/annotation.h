/*
    3dslibris - annotation.h

    A user highlight on reflowable text, with an optional note.

    Highlights are anchored by their quoted text (plus a little preceding
    context), not by page index or buffer offset: both change whenever font
    size, spacing or orientation reflow the book. page_hint/page_count_hint
    only narrow the search when the anchor is re-resolved after a relayout.
*/

#pragma once

#include <stdint.h>
#include <string>

struct Annotation {
  uint32_t id;
  uint32_t created;          // Unix timestamp; 0 if unknown.
  uint16_t page_hint;        // Page index when created or last resolved.
  uint16_t page_count_hint;  // Book page count at that time.
  std::string quote;         // Normalized selected text (UTF-8).
  std::string prefix;        // Normalized text just before the quote (UTF-8).
  std::string note;          // Optional user note (UTF-8).

  Annotation()
      : id(0), created(0), page_hint(0), page_count_hint(0) {}
};
