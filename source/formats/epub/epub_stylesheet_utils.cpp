#include "formats/epub/epub_stylesheet_utils.h"

#include "expat.h"
#include "formats/common/html_entity_utils.h"
#include "shared/string_utils.h"
#include <string.h>

namespace epub_stylesheet_utils {
namespace {

struct HeadState {
  XML_Parser parser;
  std::vector<Stylesheet> sheets;
  std::string css;
  bool in_head = false;
  bool in_style = false;
  bool complete = false;
};

std::string LocalName(const char *name) {
  const char *colon = strrchr(name, ':');
  return ToLowerAscii(colon ? colon + 1 : name);
}

std::string Attribute(const char **attrs, const char *name) {
  for (int i = 0; attrs && attrs[i]; i += 2) {
    if (LocalName(attrs[i]) == name)
      return attrs[i + 1];
  }
  return std::string();
}

void XMLCALL Start(void *user, const char *name, const char **attrs) {
  HeadState &state = *static_cast<HeadState *>(user);
  const std::string tag = LocalName(name);
  if (tag == "head")
    state.in_head = true;
  if (!state.in_head)
    return;
  const std::string type = ToLowerAscii(Trim(Attribute(attrs, "type")));
  const bool is_css = type.empty() || type == "text/css";
  if (tag == "style" && is_css) {
    state.in_style = true;
    state.css.clear();
  } else if (tag == "link" && is_css &&
             ToLowerAscii(Trim(Attribute(attrs, "rel"))) == "stylesheet") {
    Stylesheet sheet;
    sheet.href = Attribute(attrs, "href");
    if (!sheet.href.empty())
      state.sheets.push_back(sheet);
  }
}

void XMLCALL End(void *user, const char *name) {
  HeadState &state = *static_cast<HeadState *>(user);
  const std::string tag = LocalName(name);
  if (tag == "style" && state.in_style) {
    Stylesheet sheet;
    sheet.css.swap(state.css);
    state.sheets.push_back(sheet);
    state.in_style = false;
  }
  if (tag == "head") {
    state.complete = true;
    state.in_head = false;
    XML_StopParser(state.parser, XML_FALSE);
  }
}

void XMLCALL Text(void *user, const char *text, int len) {
  HeadState &state = *static_cast<HeadState *>(user);
  if (state.in_style)
    state.css.append(text, (size_t)len);
}

} // namespace

std::vector<Stylesheet> ExtractHeadStylesheets(const std::string &xhtml,
                                             bool *head_complete) {
  if (head_complete)
    *head_complete = false;
  HeadState state;
  state.parser = XML_ParserCreate(NULL);
  if (!state.parser)
    return state.sheets;
  XML_SetUserData(state.parser, &state);
  XML_SetElementHandler(state.parser, Start, End);
  XML_SetCharacterDataHandler(state.parser, Text);
  // A bounded prefix may end mid-element. Keep only completed stylesheets.
  const std::string normalized =
      html_entity_utils::NormalizeHtmlNamedEntitiesForXml(xhtml);
  XML_Parse(state.parser, normalized.data(), (int)normalized.size(), XML_FALSE);
  XML_ParserFree(state.parser);
  if (head_complete)
    *head_complete = state.complete;
  return state.sheets;
}

void MergeRules(const epub_css_class_map::CssClassMap &source,
                epub_css_class_map::CssClassMap *destination) {
  if (!destination)
    return;
  using epub_css_class_map::MarginTopResult;
  using epub_css_class_map::FontSizeSpec;
  for (const auto &entry : source) {
    const auto &src = entry.second;
    auto *out = &(*destination)[entry.first];
    if (src.margin_top.unit != MarginTopResult::Unit::None) out->margin_top = src.margin_top;
    if (src.margin_bottom.unit != MarginTopResult::Unit::None) out->margin_bottom = src.margin_bottom;
    if (src.margin_left.unit != MarginTopResult::Unit::None) out->margin_left = src.margin_left;
    if (src.margin_right.unit != MarginTopResult::Unit::None) out->margin_right = src.margin_right;
    if (src.font_size.unit != FontSizeSpec::Unit::None) out->font_size = src.font_size;
    if (src.hide_list_markers) out->hide_list_markers = true;
    if (src.has_text_align) { out->has_text_align = true; out->text_align = src.text_align; }
    if (src.has_white_space) { out->has_white_space = true; out->white_space = src.white_space; }
    if (src.superscript) out->superscript = true;
    if (src.subscript) out->subscript = true;
    if (src.page_break_before) out->page_break_before = true;
    if (src.page_break_after) out->page_break_after = true;
    if (src.page_break_inside_avoid) out->page_break_inside_avoid = true;
    if (src.has_float) { out->has_float = true; out->float_mode = src.float_mode; }
    if (src.has_clear) { out->has_clear = true; out->clear_mode = src.clear_mode; }
    if (src.no_underline) out->no_underline = true;
    if (src.force_bold) out->force_bold = true;
    if (src.force_italic) out->force_italic = true;
    if (src.reset_bold) out->reset_bold = true;
    if (src.reset_italic) out->reset_italic = true;
    if (src.text_indent.unit != MarginTopResult::Unit::None) out->text_indent = src.text_indent;
    if (src.has_text_transform) { out->has_text_transform = true; out->text_transform = src.text_transform; }
    if (src.is_display_block) out->is_display_block = true;
    if (src.is_display_none) out->is_display_none = true;
  }
}

} // namespace epub_stylesheet_utils
