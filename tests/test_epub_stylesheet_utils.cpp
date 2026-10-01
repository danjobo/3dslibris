#include "formats/epub/epub_stylesheet_utils.h"
#include "book/epub_css_class_map.h"
#include "test_assert.h"

using epub_stylesheet_utils::ExtractHeadStylesheets;

int main() {
  bool complete = false;
  const auto embedded = ExtractHeadStylesheets(
      "<html><head><title>A&nbsp;B</title><style>p{margin-left:2em;margin-bottom:1em}"
      ".center{text-align:center}.right{text-align:right}"
      "</style></head><body/></html>", &complete);
  test::ExpectTrue("head completed", complete);
  test::ExpectEq("embedded stylesheet discovered", (int)embedded.size(), 1);
  epub_css_class_map::CssClassMap rules;
  epub_css_class_map::ParseCssIntoClassMap(embedded[0].css.data(),
                                         embedded[0].css.size(), &rules);
  test::ExpectTrue("center rule loaded", rules["center"].has_text_align);
  test::ExpectEq("center alignment", (int)rules["center"].text_align,
                (int)epub_css_class_map::TextAlign::Center);
  test::ExpectEq("right alignment", (int)rules["right"].text_align,
                (int)epub_css_class_map::TextAlign::Right);
  test::ExpectTrue("publisher side margin loaded",
      rules["*p"].margin_left.unit != epub_css_class_map::MarginTopResult::Unit::None);

  const auto mixed = ExtractHeadStylesheets(
      "<html><head><!-- <link rel='stylesheet' href='fake.css'/> -->"
      "<style type='text/css'><![CDATA[.x{text-align:left}]]></style>"
      "<link rel = 'stylesheet' type = 'text/css' href = 'a&amp;b.css'/>"
      "<style>.x{text-align:right}</style>"
      "<link rel='stylesheet' type='application/x-adobe-page-template+xml' href='x.xpgt'/>"
      "<style type='text/plain'>.bad{margin-left:99px}</style>"
      "</head><body><style>.body{margin-left:99px}</style></body></html>");
  test::ExpectEq("mixed source order and filtering", (int)mixed.size(), 3);
  test::ExpectStrEq("first embedded", mixed[0].css.c_str(), ".x{text-align:left}");
  test::ExpectStrEq("link attribute entity decoded", mixed[1].href.c_str(), "a&b.css");
  test::ExpectStrEq("last embedded", mixed[2].css.c_str(), ".x{text-align:right}");

  epub_css_class_map::CssClassMap earlier, later;
  const char *base = "p{margin-left:2em;text-align:center}";
  const char *override_css = "p{text-align:right}";
  epub_css_class_map::ParseCssIntoClassMap(base, strlen(base), &earlier);
  epub_css_class_map::ParseCssIntoClassMap(override_css, strlen(override_css), &later);
  epub_stylesheet_utils::MergeRules(later, &earlier);
  test::ExpectEq("later sheet overrides alignment", (int)earlier["*p"].text_align,
                (int)epub_css_class_map::TextAlign::Right);
  test::ExpectTrue("later sheet preserves unspecified margin",
      earlier["*p"].margin_left.unit != epub_css_class_map::MarginTopResult::Unit::None);

  const auto partial = ExtractHeadStylesheets(
      "<html><head><link rel='stylesheet' href='ok.css'/><style>.x{text-align:ri",
      &complete);
  test::ExpectFalse("partial head reported", complete);
  test::ExpectEq("partial style not applied", (int)partial.size(), 1);
  const auto namespaced = ExtractHeadStylesheets(
      "<x:html xmlns:x='http://www.w3.org/1999/xhtml'><x:head>"
      "<x:style>.x{text-align:center}</x:style></x:head></x:html>");
  test::ExpectEq("namespaced style", (int)namespaced.size(), 1);
  const auto empty = ExtractHeadStylesheets("", &complete);
  test::ExpectTrue("empty input", empty.empty() && !complete);
  puts("PASS: EPUB stylesheet discovery, alignment, margins, order and partial heads");
  return 0;
}
