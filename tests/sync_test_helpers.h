// Shared test doubles for sync session tests: an in-memory connection pair
// and in-memory book files.
#pragma once

#include "sync/sync_session.h"

#include <algorithm>
#include <map>
#include <string>
#include <string.h>

// Two in-memory transports wired to each other.
class PipeTransport : public SyncTransport {
public:
  PipeTransport() : peer_(NULL), state_(kConnected), max_queued_(0) {}
  void Connect(PipeTransport *peer) { peer_ = peer; }
  void Poll(uint64_t) override {
    if (peer_ && state_ == kConnected && !outbox_.empty()) {
      peer_->inbox_ += outbox_;
      outbox_.clear();
    }
  }
  State GetState() const override { return state_; }
  void Send(const std::string &bytes) override {
    outbox_ += bytes;
    if (outbox_.size() > max_queued_)
      max_queued_ = outbox_.size();
  }
  bool SendQueueEmpty() const override { return outbox_.empty(); }
  size_t QueuedBytes() const override { return outbox_.size(); }
  std::string TakeReceived() override {
    std::string out;
    out.swap(inbox_);
    return out;
  }
  std::string Error() const override { return ""; }
  std::string PeerName() const override { return ""; }
  void Close() override { state_ = kClosed; }

  PipeTransport *peer_;
  State state_;
  std::string outbox_;
  std::string inbox_;
  size_t max_queued_;
};

// Books by sync id, read from memory.
class MemorySource : public BookFileSource {
public:
  std::map<std::string, std::string> files;
  bool Open(const std::string &sync_id, uint64_t offset,
            uint64_t *total) override {
    std::map<std::string, std::string>::const_iterator it = files.find(sync_id);
    if (it == files.end())
      return false;
    current_ = &it->second;
    pos_ = (size_t)offset;
    *total = current_->size();
    return true;
  }
  size_t Read(char *buf, size_t max) override {
    if (!current_ || pos_ >= current_->size())
      return 0;
    const size_t n = std::min(max, current_->size() - pos_);
    memcpy(buf, current_->data() + pos_, n);
    pos_ += n;
    return n;
  }
  void Close() override { current_ = NULL; }

private:
  const std::string *current_ = NULL;
  size_t pos_ = 0;
};

// Received books by file name; partial[] simulates an interrupted copy.
class MemorySink : public BookFileSink {
public:
  std::map<std::string, std::string> complete;
  std::map<std::string, std::string> partial;
  std::string refuse; // file name to refuse (e.g. "no space")
  bool fail_writes = false;

  bool Begin(const sync_manifest::BookEntry &book, uint64_t *resume_offset,
             std::string *error) override {
    if (book.file_name == refuse) {
      *error = "not enough space";
      return false;
    }
    name_ = book.file_name;
    *resume_offset = partial[name_].size();
    return true;
  }
  bool Write(const char *data, size_t len) override {
    if (fail_writes)
      return false;
    partial[name_].append(data, len);
    return true;
  }
  bool Finish(bool done) override {
    if (done) {
      complete[name_] = partial[name_];
      partial.erase(name_);
    }
    return true;
  }

private:
  std::string name_;
};
