/*
    3dslibris - connection_test.h

    Diagnostic: can this console reach Readwise (and Hardcover) over HTTPS
    with the system's HTTP/SSL services? The 3DS's TLS support and root
    certificates are old, so this decides how a direct Readwise upload has
    to be built. Blocking; device only.
*/

#pragma once

#include <string>
#include <vector>

namespace connection_test {

// Runs the requests and returns one human-readable line per request.
std::vector<std::string> Run();

} // namespace connection_test
