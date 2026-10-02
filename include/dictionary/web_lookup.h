/*
    3dslibris - web_lookup.h

    Online word lookup: Wiktionary's definitions and the Wikipedia summary
    for a word, fetched over the bundled HTTPS stack (app/https_client) and
    turned into plain text. Blocking, a few seconds on Old 3DS (one TLS
    handshake per site); device only.
*/

#pragma once

#include <string>
#include <vector>

namespace web_lookup {

struct Section {
  std::string title; // e.g. "Wiktionary: dog"
  std::string text;
  std::string url;   // page to read more, may be empty
};

struct Result {
  std::vector<Section> sections;
  std::string error; // set when nothing could be fetched
};

// word: as picked on the page (case kept for names and places).
void Lookup(const std::string &word, Result *out);

} // namespace web_lookup
