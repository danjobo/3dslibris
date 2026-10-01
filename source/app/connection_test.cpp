/*
    3dslibris - connection_test.cpp

    See include/app/connection_test.h.
*/

#include "app/connection_test.h"

#include <3ds.h>
#include <stdio.h>

#include "app/https_client.h"

namespace connection_test {

namespace {

const u64 kResponseTimeoutNs = 15ULL * 1000 * 1000 * 1000;

// A plain request through the system HTTP service: shows whether the
// network itself works. (Its HTTPS fails with D8A0A03C on modern servers.)
std::string SystemHttpProbe(const char *label, const char *url) {
  char line[160];
  Result rc = httpcInit(0);
  if (R_FAILED(rc)) {
    snprintf(line, sizeof(line), "%s: HTTP service unavailable (%08lX)",
             label, (unsigned long)rc);
    return line;
  }
  httpcContext ctx;
  rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
  u32 status = 0;
  if (R_SUCCEEDED(rc)) {
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "3dslibris");
    rc = httpcBeginRequest(&ctx);
    if (R_SUCCEEDED(rc))
      rc = httpcGetResponseStatusCodeTimeout(&ctx, &status,
                                             kResponseTimeoutNs);
    if (R_FAILED(rc))
      httpcCancelConnection(&ctx);
    httpcCloseContext(&ctx);
  }
  httpcExit();
  if (R_FAILED(rc))
    snprintf(line, sizeof(line), "%s: FAILED (error %08lX)", label,
             (unsigned long)rc);
  else
    snprintf(line, sizeof(line), "%s: OK (HTTP %lu)", label,
             (unsigned long)status);
  return line;
}

// HTTPS through the bundled TLS stack, with certificate checks.
std::string BundledTlsProbe(const char *label, const char *url) {
  const u64 start = osGetTime();
  https_client::Response response;
  const bool ok = https_client::Request("GET", url,
                                        std::vector<std::string>(), "",
                                        &response, 30);
  const double seconds = (double)(osGetTime() - start) / 1000.0;
  char line[256];
  if (ok)
    snprintf(line, sizeof(line), "%s: OK (HTTP %ld, %.1f s)", label,
             response.status, seconds);
  else
    snprintf(line, sizeof(line), "%s: FAILED %s", label,
             response.error.c_str());
  return line;
}

} // namespace

std::vector<std::string> Run() {
  std::vector<std::string> lines;
  lines.push_back(SystemHttpProbe("Network (plain HTTP)", "http://readwise.io/"));
  https_client::Session session;
  if (!session.ok()) {
    lines.push_back("Bundled HTTPS: " + session.error());
    return lines;
  }
  // Without a token Readwise answers 401: that proves TLS works.
  lines.push_back(
      BundledTlsProbe("Readwise", "https://readwise.io/api/v2/auth/"));
  lines.push_back(
      BundledTlsProbe("Hardcover", "https://api.hardcover.app/v1/graphql"));
  return lines;
}

} // namespace connection_test
