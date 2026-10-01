/*
    3dslibris - https_client.cpp

    See include/app/https_client.h.
*/

#include "app/https_client.h"

#include <3ds.h>
#include <curl/curl.h>
#include <algorithm>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "shared/path_constants.h"

namespace https_client {

namespace {

const u32 kSocBufferSize = 0x100000;
const char kRomfsCaBundle[] = "romfs:/3ds/3dslibris/resources/cacert.pem";

size_t CollectBody(char *data, size_t size, size_t count, void *user) {
  std::string *body = static_cast<std::string *>(user);
  const size_t n = size * count;
  if (body->size() < kMaxBodyBytes)
    body->append(data, std::min(n, kMaxBodyBytes - body->size()));
  return n; // keep reading (and discard) past the cap
}

bool FileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

std::string CaBundlePath() {
  const std::string sd = paths::GetResourceDir() + "/cacert.pem";
  return FileExists(sd) ? sd : std::string(kRomfsCaBundle);
}

Session::Session()
    : soc_buffer_(NULL), soc_ready_(false), curl_ready_(false), ok_(false) {
  soc_buffer_ = memalign(0x1000, kSocBufferSize);
  if (!soc_buffer_) {
    error_ = "Out of memory";
    return;
  }
  const Result rc = socInit((u32 *)soc_buffer_, kSocBufferSize);
  if (R_FAILED(rc)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Sockets unavailable (error %08lX)",
             (unsigned long)rc);
    error_ = buf;
    free(soc_buffer_);
    soc_buffer_ = NULL;
    return;
  }
  soc_ready_ = true;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    error_ = "HTTPS library failed to start";
    return;
  }
  curl_ready_ = true;
  ok_ = true;
}

Session::~Session() {
  if (curl_ready_)
    curl_global_cleanup();
  if (soc_ready_)
    socExit();
  free(soc_buffer_);
}

bool Request(const std::string &method, const std::string &url,
             const std::vector<std::string> &headers, const std::string &body,
             Response *out, long timeout_seconds) {
  *out = Response();
  CURL *curl = curl_easy_init();
  if (!curl) {
    out->error = "Couldn't start the request";
    return false;
  }
  char error_buf[CURL_ERROR_SIZE] = {0};
  const std::string ca = CaBundlePath();
  struct curl_slist *header_list = NULL;
  for (size_t i = 0; i < headers.size(); i++)
    header_list = curl_slist_append(header_list, headers[i].c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "3dslibris");
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CollectBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out->body);
  if (header_list)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
  } else if (method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  }

  const CURLcode rc = curl_easy_perform(curl);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
  } else {
    char buf[CURL_ERROR_SIZE + 48];
    snprintf(buf, sizeof(buf), "%s (curl %d)",
             error_buf[0] ? error_buf : curl_easy_strerror(rc), (int)rc);
    out->error = buf;
  }
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return rc == CURLE_OK;
}

} // namespace https_client
