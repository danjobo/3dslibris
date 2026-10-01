/*
    3dslibris - sync_book_files.h

    Book files on the SD card for a sync: finding every book in the library
    (all subfolders), reading them for the other console, and saving books
    received from it. Plain POSIX / stdio, so it is host-tested.

    A received book is written to a hidden ".sync-<crc>.part" file next to
    where it will go and renamed once complete; an interrupted copy resumes
    from that file.
*/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "sync/sync_session.h"

namespace sync_book_files {

struct LocalBook {
  std::string folder; // e.g. sdmc:/3ds/3dslibris/book/manga
  std::string file_name;
  uint64_t size;

  LocalBook() : size(0) {}
  std::string Path() const { return folder + "/" + file_name; }
  std::string SyncId() const;
};

// True for files the library shows as books.
typedef bool (*BookFilter)(const char *file_name);
// Maps a directory entry's name to the one used to open it (see
// utf8_utils::NormalizeFsFilenameForIo).
typedef std::string (*NameFn)(const std::string &raw_name);

// Every book under roots (searched in order, subfolders included). When the
// same file (name and size) is in several places the first one wins.
std::vector<LocalBook> ScanLibrary(const std::vector<std::string> &roots,
                                   BookFilter accept, NameFn normalize = NULL);

// Name of the partial file a book is received into (in the same folder).
std::string PartFileName(const sync_manifest::BookEntry &book);

// A received file name is used as-is on this SD card: no folders, no
// hidden or partial files, nothing a FAT file system rejects.
bool IsSafeFileName(const std::string &name);

} // namespace sync_book_files

// Serves books found by ScanLibrary().
class FileBookSource : public BookFileSource {
public:
  explicit FileBookSource(const std::vector<sync_book_files::LocalBook> &books)
      : books_(books), file_(NULL) {}
  ~FileBookSource() { Close(); }
  bool Open(const std::string &sync_id, uint64_t offset,
            uint64_t *total) override;
  size_t Read(char *buf, size_t max) override;
  void Close() override;

private:
  std::vector<sync_book_files::LocalBook> books_;
  FILE *file_;
};

// Saves received books into dest_dir.
class FileBookSink : public BookFileSink {
public:
  // free_bytes returns the space left on the card (or UINT64_MAX if
  // unknown); NULL skips the check.
  typedef uint64_t (*FreeSpaceFn)(void *user);
  static const uint64_t kSpareBytes = 1024 * 1024;

  FileBookSink(const std::string &dest_dir, sync_book_files::BookFilter accept,
               FreeSpaceFn free_bytes = NULL, void *user = NULL)
      : dest_dir_(dest_dir), accept_(accept), free_bytes_(free_bytes),
        user_(user), file_(NULL), expected_size_(0) {}
  ~FileBookSink() { Finish(false); }

  bool Begin(const sync_manifest::BookEntry &book, uint64_t *resume_offset,
             std::string *error) override;
  bool Write(const char *data, size_t len) override;
  bool Finish(bool complete) override;

  // Where saved books go.
  const std::string &dest_dir() const { return dest_dir_; }

private:
  std::string dest_dir_;
  sync_book_files::BookFilter accept_;
  FreeSpaceFn free_bytes_;
  void *user_;
  FILE *file_;
  std::string part_path_;
  std::string final_path_;
  uint64_t expected_size_;
};
