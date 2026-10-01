#pragma once

#include "book/epub_css_class_map.h"
#include <string>
#include <vector>

namespace epub_stylesheet_utils {

struct Stylesheet {
  std::string href; // Empty for an embedded <style> block.
  std::string css;
};

// Reads only the supplied XHTML head prefix. Completed sheets are returned in
// document order; an unfinished <style> block is never applied.
std::vector<Stylesheet> ExtractHeadStylesheets(const std::string &xhtml,
                                             bool *head_complete = nullptr);

// Later sheets override only properties they specify.
void MergeRules(const epub_css_class_map::CssClassMap &source,
                epub_css_class_map::CssClassMap *destination);

} // namespace epub_stylesheet_utils
