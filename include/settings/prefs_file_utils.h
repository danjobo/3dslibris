#pragma once
#include <cstdio>
#include <string>

namespace prefs_file_utils {
// Backup is used only when the primary is missing (interrupted replacement).
FILE *OpenForRead(const std::string &path, const std::string &backup);
// Consumes/closes fp. Never truncates the current preferences in place.
bool Commit(FILE *fp, const std::string &path, const std::string &temporary,
            const std::string &backup);
}
