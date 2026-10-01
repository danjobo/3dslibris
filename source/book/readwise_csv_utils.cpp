#include "book/readwise_csv_utils.h"

#include <stdio.h>

namespace readwise_csv_utils {

namespace {

struct CivilTime {
  int year, month, day, hour, minute, second;
};

// Days-since-epoch to civil date (Howard Hinnant's algorithm); avoids
// depending on the C library's timezone handling.
CivilTime ToCivil(uint32_t seconds) {
  CivilTime t;
  const long days = (long)(seconds / 86400u);
  const long rem = (long)(seconds % 86400u);
  t.hour = (int)(rem / 3600);
  t.minute = (int)((rem % 3600) / 60);
  t.second = (int)(rem % 60);

  const long z = days + 719468;
  const long era = z / 146097;
  const long doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long y = yoe + era * 400;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153;
  t.day = (int)(doy - (153 * mp + 2) / 5 + 1);
  t.month = (int)(mp < 10 ? mp + 3 : mp - 9);
  t.year = (int)(y + (t.month <= 2 ? 1 : 0));
  return t;
}

} // namespace

std::string QuoteField(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '"')
      out.push_back('"');
    out.push_back(value[i]);
  }
  out.push_back('"');
  return out;
}

std::string FormatDate(uint32_t seconds) {
  const CivilTime t = ToCivil(seconds);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month,
           t.day, t.hour, t.minute, t.second);
  return buf;
}

std::string HeaderLine() {
  return "Highlight,Title,Author,Note,Location,Date";
}

std::string FormatRow(const Row &row) {
  std::string line = QuoteField(row.highlight);
  line += ",";
  line += QuoteField(row.title);
  line += ",";
  line += QuoteField(row.author);
  line += ",";
  line += QuoteField(row.note);
  line += ",";
  if (row.location > 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", row.location);
    line += buf;
  }
  line += ",";
  if (row.date != 0)
    line += QuoteField(FormatDate(row.date));
  return line;
}

std::string BuildCsv(const std::vector<Row> &rows) {
  std::string out = HeaderLine();
  out += "\r\n";
  for (size_t i = 0; i < rows.size(); i++) {
    out += FormatRow(rows[i]);
    out += "\r\n";
  }
  return out;
}

std::string BuildFileName(uint32_t seconds) {
  const CivilTime t = ToCivil(seconds);
  char buf[48];
  snprintf(buf, sizeof(buf), "readwise-%04d%02d%02d-%02d%02d%02d.csv", t.year,
           t.month, t.day, t.hour, t.minute, t.second);
  return buf;
}

} // namespace readwise_csv_utils
