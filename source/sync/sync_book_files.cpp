/*
    3dslibris - sync_book_files.cpp

    See include/sync/sync_book_files.h.
*/

#include "sync/sync_book_files.h"

#include <dirent.h>
#include <set>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sync/sync_merge.h"

namespace sync_book_files {

namespace {

const int kMaxDepth = 8;
const char kPartSuffix[] = ".part";

bool EndsWith(const std::string &s, const char *suffix) {
  const std::string tail(suffix);
  return s.size() >= tail.size() &&
         s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void ScanDir(const std::string &dir, int depth, BookFilter accept,
             NameFn normalize, std::set<std::string> *seen,
             std::vector<LocalBook> *out) {
  DIR *dp = opendir(dir.c_str());
  if (!dp)
    return;
  std::vector<std::string> subdirs;
  struct dirent *ent;
  while ((ent = readdir(dp))) {
    if (ent->d_name[0] == '.')
      continue;
    const std::string name =
        normalize ? normalize(ent->d_name) : std::string(ent->d_name);
    const std::string path = dir + "/" + name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode)) {
      subdirs.push_back(path);
      continue;
    }
    if (!S_ISREG(st.st_mode) || EndsWith(name, kPartSuffix) ||
        (accept && !accept(name.c_str())))
      continue;
    LocalBook book;
    book.folder = dir;
    book.file_name = name;
    book.size = (uint64_t)st.st_size;
    if (seen->insert(book.SyncId()).second)
      out->push_back(book);
  }
  closedir(dp);
  if (depth >= kMaxDepth)
    return;
  for (size_t i = 0; i < subdirs.size(); i++)
    ScanDir(subdirs[i], depth + 1, accept, normalize, seen, out);
}

bool FileSize(const std::string &path, uint64_t *size) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  *size = (uint64_t)st.st_size;
  return true;
}

} // namespace

std::string LocalBook::SyncId() const {
  return sync_merge::MakeSyncBookId(file_name, size);
}

std::vector<LocalBook> ScanLibrary(const std::vector<std::string> &roots,
                                   BookFilter accept, NameFn normalize) {
  std::vector<LocalBook> out;
  std::set<std::string> seen;
  for (size_t i = 0; i < roots.size(); i++)
    ScanDir(roots[i], 0, accept, normalize, &seen, &out);
  return out;
}

bool IsSafeFileName(const std::string &name) {
  if (name.empty() || name.size() > 200 || name[0] == '.' || name[0] == ' ' ||
      EndsWith(name, kPartSuffix) || EndsWith(name, ".") ||
      EndsWith(name, " "))
    return false;
  for (size_t i = 0; i < name.size(); i++) {
    const unsigned char c = (unsigned char)name[i];
    if (c < 0x20 || c == 0x7F)
      return false;
    switch (c) {
    case '/':
    case '\\':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
      return false;
    default:
      break;
    }
  }
  return true;
}

} // namespace sync_book_files

bool FileBookSource::Open(const std::string &sync_id, uint64_t offset,
                          uint64_t *total) {
  Close();
  for (size_t i = 0; i < books_.size(); i++) {
    if (books_[i].SyncId() != sync_id)
      continue;
    file_ = fopen(books_[i].Path().c_str(), "rb");
    if (!file_)
      return false;
    if (offset > books_[i].size ||
        fseek(file_, (long)offset, SEEK_SET) != 0) {
      Close();
      return false;
    }
    *total = books_[i].size;
    return true;
  }
  return false;
}

size_t FileBookSource::Read(char *buf, size_t max) {
  return file_ ? fread(buf, 1, max, file_) : 0;
}

void FileBookSource::Close() {
  if (file_)
    fclose(file_);
  file_ = NULL;
}

bool FileBookSink::Begin(const sync_manifest::BookEntry &book,
                         uint64_t *resume_offset, std::string *error) {
  Finish(false);
  if (!sync_book_files::IsSafeFileName(book.file_name) ||
      (accept_ && !accept_(book.file_name.c_str()))) {
    *error = "file name can't be used here";
    return false;
  }
  final_path_ = dest_dir_ + "/" + book.file_name;
  part_path_ = final_path_ + ".part";
  expected_size_ = book.file_size;
  uint64_t existing = 0;
  if (sync_book_files::FileSize(final_path_, &existing)) {
    *error = "a different book with this name is already here";
    return false;
  }
  uint64_t have = 0;
  if (!sync_book_files::FileSize(part_path_, &have) || have > book.file_size)
    have = 0; // nothing to resume, or a different file's leftovers
  if (free_bytes_) {
    const uint64_t free_space = free_bytes_(user_);
    const uint64_t needed = book.file_size - have + kSpareBytes;
    if (free_space < needed) {
      *error = "not enough space on the SD card";
      return false;
    }
  }
  file_ = fopen(part_path_.c_str(), have > 0 ? "ab" : "wb");
  if (!file_) {
    *error = "can't create the file";
    return false;
  }
  *resume_offset = have;
  return true;
}

bool FileBookSink::Write(const char *data, size_t len) {
  return file_ && fwrite(data, 1, len, file_) == len;
}

bool FileBookSink::Finish(bool complete) {
  bool ok = true;
  if (file_) {
    ok = fclose(file_) == 0;
    file_ = NULL;
  }
  if (!complete || part_path_.empty())
    return ok;
  uint64_t size = 0;
  if (!ok || !sync_book_files::FileSize(part_path_, &size) ||
      size != expected_size_) {
    remove(part_path_.c_str()); // damaged; start over next time
    part_path_.clear();
    return false;
  }
  ok = rename(part_path_.c_str(), final_path_.c_str()) == 0;
  part_path_.clear();
  return ok;
}
