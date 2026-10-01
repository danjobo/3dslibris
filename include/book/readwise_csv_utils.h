/*
    3dslibris - readwise_csv_utils.h

    Builds a CSV of highlights in Readwise's import format:
    https://docs.readwise.io/readwise/docs/importing-highlights
    A header row names the columns (any order). Highlight is required; Title,
    Author, Note, Location (integer) and Date ("YYYY-MM-DD HH:MM:SS", read as
    UTC) are optional.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace readwise_csv_utils {

struct Row {
  std::string highlight;
  std::string title;
  std::string author;
  std::string note;
  int location;   // 1-based page; <= 0 leaves the column empty
  uint32_t date;  // seconds since 1970 as kept by the console clock; 0 = none

  Row() : location(0), date(0) {}
};

// RFC 4180 field: always quoted, embedded quotes doubled.
std::string QuoteField(const std::string &value);

// "YYYY-MM-DD HH:MM:SS" for a seconds-since-1970 value, without timezone
// conversion (the 3DS clock keeps local wall time).
std::string FormatDate(uint32_t seconds);

std::string HeaderLine();
std::string FormatRow(const Row &row);
// Header plus one line per row, CRLF line endings.
std::string BuildCsv(const std::vector<Row> &rows);

// "readwise-YYYYMMDD-HHMMSS.csv"
std::string BuildFileName(uint32_t seconds);

} // namespace readwise_csv_utils
