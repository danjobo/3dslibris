/*
    3dslibris - sync_transport.h

    A byte stream between two consoles (Wi-Fi TCP now, local wireless
    later). Everything is non-blocking: Poll() is called once per frame and
    does a bounded amount of connecting, sending and receiving, so the main
    loop (and HOME / sleep handling) never stalls.
*/

#pragma once

#include <stdint.h>
#include <string>

class SyncTransport {
public:
  enum State {
    kWaiting,   // host: waiting for a joiner; join: searching for a host
    kConnected, // stream is up
    kFailed,    // error(); unusable
    kClosed,    // peer closed the stream, or Close() was called
  };

  virtual ~SyncTransport() {}
  virtual void Poll(uint64_t now_ms) = 0;
  virtual State GetState() const = 0;
  // Queues bytes for sending (sent during Poll).
  virtual void Send(const std::string &bytes) = 0;
  virtual bool SendQueueEmpty() const = 0;
  // Returns and clears the bytes received so far.
  virtual std::string TakeReceived() = 0;
  virtual std::string Error() const = 0;
  // Name the peer announced during discovery, if any.
  virtual std::string PeerName() const = 0;
  virtual void Close() = 0;
};
