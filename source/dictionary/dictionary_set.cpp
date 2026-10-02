/*
    3dslibris - dictionary_set.cpp

    See include/dictionary/dictionary_set.h.
*/

#include "dictionary/dictionary_set.h"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#include "dictionary/word_lookup_utils.h"

namespace dictionary {

namespace {

bool EndsWithIfo(const std::string &name) {
  if (name.size() < 5)
    return false;
  std::string ext = name.substr(name.size() - 4);
  for (size_t i = 0; i < ext.size(); i++)
    ext[i] = (char)tolower((unsigned char)ext[i]);
  return ext == ".ifo";
}

bool IsDirectory(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Directory entries, sorted so dictionaries keep a stable order.
std::vector<std::string> ListDirectory(const std::string &dir) {
  std::vector<std::string> names;
  DIR *d = opendir(dir.c_str());
  if (!d)
    return names;
  while (struct dirent *e = readdir(d)) {
    const std::string name = e->d_name;
    if (name != "." && name != "..")
      names.push_back(name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  return names;
}

} // namespace

void DictionarySet::AddIfo(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  const std::string name =
      slash == std::string::npos ? path : path.substr(slash + 1);
  if (std::find(names_.begin(), names_.end(), name) != names_.end())
    return;
  std::unique_ptr<stardict::Dictionary> dict(new stardict::Dictionary());
  std::string error;
  if (!dict->Open(path, &error)) {
    errors_.push_back(name + ": " + error);
    return;
  }
  names_.push_back(name);
  dictionaries_.push_back(std::move(dict));
}

void DictionarySet::AddDirectory(const std::string &dir) {
  const std::vector<std::string> names = ListDirectory(dir);
  std::vector<std::string> subdirs;
  for (size_t i = 0; i < names.size(); i++) {
    const std::string path = dir + "/" + names[i];
    if (EndsWithIfo(names[i]))
      AddIfo(path);
    else if (IsDirectory(path))
      subdirs.push_back(path);
  }
  for (size_t s = 0; s < subdirs.size(); s++) {
    const std::vector<std::string> inner = ListDirectory(subdirs[s]);
    for (size_t i = 0; i < inner.size(); i++)
      if (EndsWithIfo(inner[i]))
        AddIfo(subdirs[s] + "/" + inner[i]);
  }
}

size_t DictionarySet::LookupForm(
    const std::string &form, size_t max_results, std::vector<Result> *out,
    std::vector<std::pair<size_t, uint32_t> > *seen) {
  size_t added = 0;
  for (size_t d = 0; d < dictionaries_.size() && out->size() < max_results;
       d++) {
    std::vector<stardict::Article> articles;
    dictionaries_[d]->Lookup(form, max_results, &articles);
    for (size_t a = 0; a < articles.size() && out->size() < max_results; a++) {
      const std::pair<size_t, uint32_t> key(d, articles[a].offset);
      if (std::find(seen->begin(), seen->end(), key) != seen->end())
        continue;
      seen->push_back(key);
      Result r;
      r.dictionary = dictionaries_[d]->name();
      r.headword = articles[a].headword;
      r.text = articles[a].text;
      out->push_back(r);
      added++;
    }
  }
  return added;
}

std::vector<Result> DictionarySet::Lookup(const std::string &word,
                                          size_t max_results) {
  std::vector<Result> out;
  std::vector<std::pair<size_t, uint32_t> > seen;
  if (word.empty())
    return out;
  const std::string lower = word_lookup_utils::ToLower(word);
  LookupForm(word, max_results, &out, &seen);
  if (lower != word)
    LookupForm(lower, max_results, &out, &seen);
  if (!out.empty())
    return out;
  // Not a headword: try what it could be an inflection of. Every form that
  // is found is shown ("hoped" -> "hop" and "hope"); the reader picks.
  const std::vector<std::string> forms =
      word_lookup_utils::BaseFormCandidates(lower);
  for (size_t i = 0; i < forms.size() && out.size() < max_results; i++)
    LookupForm(forms[i], max_results, &out, &seen);
  return out;
}

} // namespace dictionary
