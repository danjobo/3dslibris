#include "settings/prefs_file_utils.h"
#include <cerrno>
#include <sys/stat.h>

namespace prefs_file_utils {
FILE *OpenForRead(const std::string &path, const std::string &backup) {
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp && errno == ENOENT)
    fp = fopen(backup.c_str(), "rb");
  return fp;
}

bool Commit(FILE *fp, const std::string &path, const std::string &temporary,
            const std::string &backup) {
  if (!fp)
    return false;
  bool ok = ferror(fp) == 0;
  if (fflush(fp) != 0)
    ok = false;
  if (fclose(fp) != 0)
    ok = false;
  if (!ok) {
    remove(temporary.c_str());
    return false;
  }

  struct stat info;
  const bool had_current = stat(path.c_str(), &info) == 0;
  if (!had_current && errno != ENOENT)
    return false;
  if (had_current && !S_ISREG(info.st_mode))
    return false;
  if (had_current) {
    // Rename onto an existing file is not supported by every SD backend.
    // Preserve the current file before installing the completed temporary.
    if (remove(backup.c_str()) != 0 && errno != ENOENT)
      return false;
    if (rename(path.c_str(), backup.c_str()) != 0)
      return false;
  }
  if (rename(temporary.c_str(), path.c_str()) != 0) {
    if (had_current)
      rename(backup.c_str(), path.c_str());
    // If rollback fails, OpenForRead can still recover the backup.
    return false;
  }
  return true;
}
}
