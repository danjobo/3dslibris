/*
    3dslibris - readwise_api_utils.h

    Pure helpers for uploading highlights to Readwise's API
    ("Authorization: Token <token>"): request bodies, ISO 8601 times, and
    deciding what each highlight needs. Host-tested.

    - New highlights are created (POST /api/v2/highlights/) with their
      color as an inline tag: the note starts with ".green" (Readwise turns
      a leading ".word" into a tag), then the note text on the next line.
    - Readwise ignores a re-sent highlight with the same text, title and
      author, so a highlight edited after upload (note or color) is updated
      by its Readwise id instead: PATCH the note, swap the color tag.
    - Which version was uploaded, and Readwise's id, are stored on each
      highlight (Annotation::readwise_uploaded / readwise_id) and synced
      between consoles. The older per-console log (readwise-uploaded.txt)
      still counts: those highlights exist in Readwise but have no color tag
      yet, so they get one on the next upload.
    Deleting a highlight here doesn't delete it in Readwise.
*/

#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace readwise_api_utils {

struct Highlight {
  uint64_t id;
  uint32_t modified;
  std::string text;
  std::string title;
  std::string author;
  std::string note;
  int location;          // 1-based page
  uint32_t highlighted_at; // Unix time; 0 = unknown (field omitted)
  uint8_t color;         // highlight_color_utils::Color
  uint32_t readwise_uploaded; // see Annotation
  uint64_t readwise_id;
  // The book it belongs to here (to store the upload state back).
  std::string folder;
  std::string file_name;

  Highlight()
      : id(0), modified(0), location(0), highlighted_at(0), color(0),
        readwise_uploaded(0), readwise_id(0) {}
};

// Readwise's field limits (characters); longer values are cut.
static const size_t kMaxTextChars = 8191;
static const size_t kMaxNoteChars = 8191;
static const size_t kMaxTitleChars = 511;
static const size_t kMaxAuthorChars = 1024;
// Highlights per request.
static const size_t kBatchSize = 100;

std::string JsonString(const std::string &utf8);
// "2026-10-01T12:34:56+00:00". The 3DS clock has no time zone; its local
// time is sent as UTC (the same as the CSV export).
std::string IsoTime(uint32_t unix_time);
// {"highlights":[{...}, ...]}
std::string BuildHighlightsJson(const std::vector<Highlight> &highlights);

// The color's tag name ("yellow", "green", ...; Readwise's color names).
std::string ColorTag(uint8_t color);
// The note as uploaded: ".<color>", then the note text on the next line.
std::string NoteWithTag(uint8_t color, const std::string &note);

// The older per-console upload log: id -> modified time last uploaded.
typedef std::map<uint64_t, uint32_t> UploadLog;
std::string SerializeLog(const UploadLog &log);
// Unknown or damaged input gives an empty log.
UploadLog ParseLog(const std::string &data);

// What each highlight needs.
struct Work {
  std::vector<Highlight> create; // not in Readwise yet
  std::vector<Highlight> update; // in Readwise; note and/or color to update
  // For each update: whether its note may have changed since it was sent
  // (false for older uploads whose only missing piece is the color tag).
  std::vector<bool> update_note;
};
Work Classify(const std::vector<Highlight> &all, const UploadLog &legacy_log);

// Readwise's stored text may differ in surrounding whitespace.
bool SameText(const std::string &a, const std::string &b);

// {"note": ...} for PATCH /api/v2/highlights/<id>/.
std::string PatchNoteJson(const std::string &note);
// {"name": ...} for POST /api/v2/highlights/<id>/tags/.
std::string TagJson(const std::string &name);

// The token file's contents without whitespace or line breaks.
std::string CleanToken(const std::string &raw);

} // namespace readwise_api_utils
