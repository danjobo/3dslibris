/*
    3dslibris - https_client.h

    HTTPS requests with a bundled TLS stack (libcurl + mbedTLS from the
    devkitPro portlibs) and Mozilla's root certificates
    (resources/cacert.pem). The 3DS's own SSL service can't negotiate with
    modern servers (error D8A0A03C), so web services such as Readwise go
    through this instead. Blocking; device only.
*/

#pragma once

#include <string>
#include <vector>

namespace https_client {

// Sockets and libcurl for the lifetime of the object. Requests made
// through one Session reuse its connection, so only the first one to a
// server pays for the TLS handshake (~2.5 s on Old 3DS).
class Session {
public:
  Session();
  ~Session();
  bool ok() const { return ok_; }
  const std::string &error() const { return error_; }
  // False when the console isn't connected to a network.
  bool HasNetwork() const;
  void *handle() const { return handle_; }

private:
  void *soc_buffer_;
  bool soc_ready_;
  bool curl_ready_;
  void *handle_; // CURL easy handle, kept for its connection
  bool ok_;
  std::string error_;
};

struct Response {
  long status;        // HTTP status, 0 if no response
  std::string body;   // first kMaxBodyBytes of it
  std::string error;  // set when the request failed (no HTTP status)
  Response() : status(0) {}
};

static const size_t kMaxBodyBytes = 64 * 1024;

// method: "GET", "POST", ... headers: "Name: value".
bool Request(Session &session, const std::string &method,
             const std::string &url,
             const std::vector<std::string> &headers, const std::string &body,
             Response *out, long timeout_seconds = 30);

// The CA bundle in use (SD copy first, then the one built into the app).
std::string CaBundlePath();

} // namespace https_client
