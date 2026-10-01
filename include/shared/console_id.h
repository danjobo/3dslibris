/*
    3dslibris - console_id.h

    A random identifier for this console, created on first use and kept in
    paths::GetSdmcBase()/console_id.txt. Record ids (highlights, bookmarks)
    carry its 32-bit prefix so records created on different consoles never
    collide when synced.
*/

#pragma once

#include <stdint.h>
#include <string>

namespace console_id {

// This console's id (loaded or created on first call, then cached).
uint64_t Get();
// Non-zero 32-bit prefix for record ids.
uint32_t Prefix();

// Pure helpers (host-tested).
uint64_t Generate(uint64_t entropy);
uint32_t PrefixFor(uint64_t id);
std::string Format(uint64_t id);
bool Parse(const std::string &text, uint64_t *out);

} // namespace console_id
