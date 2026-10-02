/*
    3dslibris - web_lookup.cpp

    See include/dictionary/web_lookup.h.
*/

#include "dictionary/web_lookup.h"

#include <jansson.h>
#include <string.h>

#include "app/https_client.h"
#include "app/version.h"
#include "dictionary/word_lookup_utils.h"

namespace web_lookup {

namespace {

// Wiktionary's answer for a common word ("set", "run") is several hundred KB.
const size_t kMaxResponseBytes = 1024 * 1024;
const size_t kMaxLanguages = 3;
const size_t kMaxPartsOfSpeech = 6;
const size_t kMaxSenses = 6;

std::vector<std::string> Headers() {
  std::vector<std::string> headers;
  // Wikimedia asks API clients to identify themselves.
  headers.push_back("User-Agent: 3dslibris/" VERSION
                    " (https://github.com/RigleGit/3dslibris)");
  headers.push_back("Accept: application/json");
  return headers;
}

std::string StringOf(json_t *v) {
  return json_is_string(v) ? json_string_value(v) : std::string();
}

void AppendPartsOfSpeech(json_t *entries, bool english, size_t *parts,
                         std::string *text) {
  for (size_t i = 0; i < json_array_size(entries) && *parts < kMaxPartsOfSpeech;
       i++) {
    json_t *entry = json_array_get(entries, i);
    std::string heading = StringOf(json_object_get(entry, "partOfSpeech"));
    const std::string language = StringOf(json_object_get(entry, "language"));
    if (!english && !language.empty())
      heading += " (" + language + ")";
    std::string senses;
    int number = 0;
    json_t *defs = json_object_get(entry, "definitions");
    for (size_t d = 0; d < json_array_size(defs) && number < (int)kMaxSenses;
         d++) {
      json_t *def = json_array_get(defs, d);
      const std::string meaning = word_lookup_utils::MarkupToText(
          StringOf(json_object_get(def, "definition")));
      if (meaning.empty())
        continue;
      senses += "\n" + std::to_string((long long)++number) + ". " + meaning;
      json_t *examples = json_object_get(def, "examples");
      if (json_array_size(examples) > 0) {
        const std::string example = word_lookup_utils::MarkupToText(
            StringOf(json_array_get(examples, 0)));
        if (!example.empty())
          senses += "\n   \"" + example + "\"";
      }
    }
    if (number == 0)
      continue;
    if (!text->empty())
      *text += "\n";
    *text += heading + senses;
    (*parts)++;
  }
}

// Status 404 means "no such page", which isn't an error worth showing.
bool Fetch(https_client::Session &session, const std::string &url,
           https_client::Response *response, std::string *error) {
  if (!https_client::Request(session, "GET", url, Headers(), std::string(),
                             response, 20, kMaxResponseBytes)) {
    *error = response->error;
    return false;
  }
  if (response->status != 200 && response->status != 404) {
    *error = "HTTP " + std::to_string((long long)response->status);
    return false;
  }
  return response->status == 200;
}

bool Wiktionary(https_client::Session &session, const std::string &word,
                Section *out, std::string *error) {
  https_client::Response response;
  if (!Fetch(session, word_lookup_utils::WiktionaryDefinitionUrl(word),
             &response, error))
    return false;
  json_t *root = json_loads(response.body.c_str(), 0, NULL);
  if (!json_is_object(root)) {
    json_decref(root);
    *error = "Unreadable Wiktionary reply";
    return false;
  }
  std::string text;
  size_t parts = 0;
  AppendPartsOfSpeech(json_object_get(root, "en"), true, &parts, &text);
  // Then other languages (a foreign word in an English book).
  size_t languages = 1;
  const char *key;
  json_t *value;
  json_object_foreach(root, key, value) {
    if (languages >= kMaxLanguages || parts >= kMaxPartsOfSpeech)
      break;
    if (strcmp(key, "en") == 0 || !json_is_array(value))
      continue;
    const size_t before = parts;
    AppendPartsOfSpeech(value, false, &parts, &text);
    if (parts > before)
      languages++;
  }
  json_decref(root);
  if (text.empty())
    return false;
  out->title = "Wiktionary: " + word;
  out->text = text;
  out->url = "https://en.wiktionary.org/wiki/" +
             word_lookup_utils::UrlEncode(word);
  return true;
}

bool Wikipedia(https_client::Session &session, const std::string &word,
               Section *out, std::string *error) {
  https_client::Response response;
  if (!Fetch(session, word_lookup_utils::WikipediaSummaryUrl(word), &response,
             error))
    return false;
  json_t *root = json_loads(response.body.c_str(), 0, NULL);
  if (!json_is_object(root)) {
    json_decref(root);
    *error = "Unreadable Wikipedia reply";
    return false;
  }
  const std::string type = StringOf(json_object_get(root, "type"));
  const std::string title = StringOf(json_object_get(root, "title"));
  const std::string description =
      StringOf(json_object_get(root, "description"));
  std::string extract = StringOf(json_object_get(root, "extract"));
  json_t *urls = json_object_get(root, "content_urls");
  const std::string url =
      StringOf(json_object_get(json_object_get(urls, "mobile"), "page"));
  json_decref(root);
  if (type == "disambiguation")
    extract = "\"" + title + "\" can mean several things; see the page.";
  if (extract.empty())
    return false;
  out->title = "Wikipedia: " + (title.empty() ? word : title);
  out->text = description.empty() ? extract : description + "\n" + extract;
  out->url = url;
  return true;
}

} // namespace

void Lookup(const std::string &word, Result *out) {
  *out = Result();
  https_client::Session session;
  if (!session.ok() || !session.HasNetwork()) {
    out->error = session.ok() ? "Not connected to Wi-Fi." : session.error();
    return;
  }
  std::string error;
  Section section;
  const std::string lower = word_lookup_utils::ToLower(word);
  if (Wiktionary(session, lower, &section, &error) ||
      (lower != word && error.empty() &&
       Wiktionary(session, word, &section, &error)))
    out->sections.push_back(section);
  section = Section();
  if (Wikipedia(session, word, &section, &error))
    out->sections.push_back(section);
  if (out->sections.empty())
    out->error = error.empty() ? "Nothing found online for \"" + word + "\"."
                               : "Lookup failed: " + error;
}

} // namespace web_lookup
