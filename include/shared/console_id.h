/*
    3dslibris - console_id.h

    An identifier for this console. On the 3DS it is derived from the
    system's per-console hash (CFGU_GenHashConsoleUnique), so it stays
    unique even if the 3dslibris folder is copied between consoles; if that
    is unavailable (and on hosts) a random id kept in
    paths::GetSdmcBase()/console_id.txt is used. Record ids (highlights,
    bookmarks) carry its 32-bit prefix so records created on different
    consoles never collide when synced.
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
