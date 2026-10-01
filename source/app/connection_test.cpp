/*
    3dslibris - connection_test.cpp

    See include/app/connection_test.h.
*/

#include "app/connection_test.h"

#include <3ds.h>
#include <stdio.h>

namespace connection_test {

namespace {

const u64 kResponseTimeoutNs = 15ULL * 1000 * 1000 * 1000;

struct Probe {
  const char *label;
  const char *url;
  bool verify; // check the server certificate against the system's roots
};

std::string Describe(const Probe &probe) {
  httpcContext ctx;
  char line[160];
  Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, probe.url, 1);
  if (R_FAILED(rc)) {
    snprintf(line, sizeof(line), "%s: can't start (error %08lX)", probe.label,
             (unsigned long)rc);
    return line;
  }
  if (!probe.verify)
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
  httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
  httpcAddRequestHeaderField(&ctx, "User-Agent", "3dslibris");
  rc = httpcBeginRequest(&ctx);
  u32 status = 0;
  if (R_SUCCEEDED(rc))
    rc = httpcGetResponseStatusCodeTimeout(&ctx, &status, kResponseTimeoutNs);
  if (R_FAILED(rc))
    httpcCancelConnection(&ctx);
  httpcCloseContext(&ctx);
  if (R_FAILED(rc))
    snprintf(line, sizeof(line), "%s: FAILED (error %08lX)", probe.label,
             (unsigned long)rc);
  else
    snprintf(line, sizeof(line), "%s: OK (HTTP %lu)", probe.label,
             (unsigned long)status);
  return line;
}

} // namespace

std::vector<std::string> Run() {
  static const Probe kProbes[] = {
      // A plain request first: shows whether the network itself works.
      {"Network (plain HTTP)", "http://readwise.io/", false},
      // Without a token the API answers 401: that still proves TLS works.
      {"Readwise, cert checked", "https://readwise.io/api/v2/auth/", true},
      {"Readwise, no cert check", "https://readwise.io/api/v2/auth/", false},
      {"Hardcover, no cert check", "https://api.hardcover.app/v1/graphql",
       false},
  };
  std::vector<std::string> lines;
  Result rc = httpcInit(0);
  if (R_FAILED(rc)) {
    char line[96];
    snprintf(line, sizeof(line), "HTTP service unavailable (error %08lX)",
             (unsigned long)rc);
    lines.push_back(line);
    return lines;
  }
  for (size_t i = 0; i < sizeof(kProbes) / sizeof(kProbes[0]); i++)
    lines.push_back(Describe(kProbes[i]));
  httpcExit();
  return lines;
}

} // namespace connection_test
