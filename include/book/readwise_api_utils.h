/*
    3dslibris - readwise_api_utils.h

    Pure helpers for uploading highlights to Readwise's API
    (POST https://readwise.io/api/v2/highlights/, "Authorization: Token
    <token>"): the JSON body, ISO 8601 times, and the upload log that
    remembers which highlights (by id and modified time) were already sent,
    so each upload only sends new or edited ones. Host-tested.

    Readwise merges a highlight it already has (same text, title and
    author), so sending one again updates its note instead of duplicating
    it. Deleting a highlight here doesn't delete it in Readwise.
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

  Highlight() : id(0), modified(0), location(0), highlighted_at(0) {}
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

// id -> modified time of the version last uploaded.
typedef std::map<uint64_t, uint32_t> UploadLog;
std::string SerializeLog(const UploadLog &log);
// Unknown or damaged input gives an empty log (everything is sent again,
// which Readwise merges).
UploadLog ParseLog(const std::string &data);
// Highlights that are new or changed since they were last uploaded.
std::vector<Highlight> Pending(const std::vector<Highlight> &all,
                               const UploadLog &log);

// The token file's contents without whitespace or line breaks.
std::string CleanToken(const std::string &raw);

} // namespace readwise_api_utils
