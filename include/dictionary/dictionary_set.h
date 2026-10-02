/*
    3dslibris - dictionary_set.h

    All StarDict dictionaries found in the dictionary folders (the SD
    card's dict/ folder, then the bundled ones in romfs), and the word
    lookup across them: the word as picked, then its lower case, and only
    if neither is found its base forms ("running" -> "run"). No 3DS
    dependencies.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dictionary/stardict.h"

namespace dictionary {

struct Result {
  std::string dictionary; // the dictionary's name
  std::string headword;
  std::string text;
};

class DictionarySet {
public:
  // Opens every .ifo in dir and in its subfolders (one level). A
  // dictionary whose .ifo file name was already added (an SD copy of a
  // bundled one) is skipped. Unreadable ones are listed in errors().
  void AddDirectory(const std::string &dir);
  size_t size() const { return dictionaries_.size(); }
  const std::vector<std::string> &errors() const { return errors_; }

  // Up to max_results articles for the word, best match first.
  std::vector<Result> Lookup(const std::string &word, size_t max_results);

private:
  void AddIfo(const std::string &path);
  // Articles for one form from every dictionary, skipping ones already in
  // out. Returns how many were added.
  size_t LookupForm(const std::string &form, size_t max_results,
                    std::vector<Result> *out,
                    std::vector<std::pair<size_t, uint32_t> > *seen);

  std::vector<std::unique_ptr<stardict::Dictionary> > dictionaries_;
  std::vector<std::string> names_;
  std::vector<std::string> errors_;
};

} // namespace dictionary
