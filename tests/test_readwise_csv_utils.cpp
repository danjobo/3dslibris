#include "book/readwise_csv_utils.h"

#include "test_assert.h"

#include <string>
#include <vector>

using readwise_csv_utils::Row;

namespace {

void TestQuoteField() {
  test::ExpectStrEq("plain", readwise_csv_utils::QuoteField("abc").c_str(),
                    "\"abc\"");
  test::ExpectStrEq("comma kept inside quotes",
                    readwise_csv_utils::QuoteField("a, b").c_str(),
                    "\"a, b\"");
  test::ExpectStrEq("quotes doubled",
                    readwise_csv_utils::QuoteField("say \"hi\"").c_str(),
                    "\"say \"\"hi\"\"\"");
  test::ExpectStrEq("newline kept inside quotes",
                    readwise_csv_utils::QuoteField("line1\nline2").c_str(),
                    "\"line1\nline2\"");
  test::ExpectStrEq("empty", readwise_csv_utils::QuoteField("").c_str(),
                    "\"\"");
}

void TestFormatDate() {
  test::ExpectStrEq("epoch", readwise_csv_utils::FormatDate(0).c_str(),
                    "1970-01-01 00:00:00");
  // 2026-09-30 17:31:39
  test::ExpectStrEq("recent",
                    readwise_csv_utils::FormatDate(1790789499u).c_str(),
                    "2026-09-30 17:31:39");
  // Leap day.
  test::ExpectStrEq("leap day",
                    readwise_csv_utils::FormatDate(1709210096u).c_str(),
                    "2024-02-29 12:34:56");
  test::ExpectStrEq("year end",
                    readwise_csv_utils::FormatDate(1767225599u).c_str(),
                    "2025-12-31 23:59:59");
}

void TestRowsAndHeader() {
  Row row;
  row.highlight = "And in the silence, \"ping\"";
  row.title = "The Forever War";
  row.author = "Filkins, Dexter";
  row.note = "re-read\nlater";
  row.location = 42;
  row.date = 1709210096u;
  test::ExpectStrEq("row", readwise_csv_utils::FormatRow(row).c_str(),
                    "\"And in the silence, \"\"ping\"\"\",\"The Forever War\","
                    "\"Filkins, Dexter\",\"re-read\nlater\",42,"
                    "\"2024-02-29 12:34:56\"");

  Row bare;
  bare.highlight = "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80";
  test::ExpectStrEq("optional columns empty",
                    readwise_csv_utils::FormatRow(bare).c_str(),
                    "\"caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80\","
                    "\"\",\"\",\"\",,");

  std::vector<Row> rows;
  test::ExpectStrEq("header only when empty",
                    readwise_csv_utils::BuildCsv(rows).c_str(),
                    "Highlight,Title,Author,Note,Location,Date\r\n");
  rows.push_back(bare);
  rows.push_back(row);
  const std::string csv = readwise_csv_utils::BuildCsv(rows);
  test::ExpectTrue("starts with header",
                   csv.find("Highlight,Title,Author,Note,Location,Date\r\n") ==
                       0);
  test::ExpectTrue("ends with CRLF",
                   csv.size() >= 2 && csv.compare(csv.size() - 2, 2, "\r\n") ==
                                          0);
}

void TestFileName() {
  test::ExpectStrEq("file name",
                    readwise_csv_utils::BuildFileName(1709210096u).c_str(),
                    "readwise-20240229-123456.csv");
}

} // namespace

int main() {
  TestQuoteField();
  TestFormatDate();
  TestRowsAndHeader();
  TestFileName();
  return 0;
}
