/*
    3dslibris - wifi_transport.h

    Sync over a Wi-Fi network both consoles are on. The host listens for a
    TCP connection and answers UDP discovery broadcasts; the joiner
    broadcasts until a host answers, then connects. Plain BSD sockets
    (non-blocking), so the same code runs on the 3DS (after socInit) and on
    a PC for tests.
*/

#pragma once

#include <stdint.h>
#include <string>

#include "sync/sync_transport.h"

class WifiTransport : public SyncTransport {
public:
  static const uint16_t kDiscoveryPort = 47210;
  static const uint16_t kStreamPort = 47211;

  struct Options {
    uint16_t discovery_port;
    uint16_t stream_port;
    // Join only: send discovery here instead of broadcasting (tests,
    // loopback). Empty: broadcast on the local network.
    std::string direct_host;
    // This console's IPv4 address in host byte order (0 if unknown); used
    // to also send to the /24 subnet broadcast address.
    uint32_t local_ip;

    Options()
        : discovery_port(kDiscoveryPort), stream_port(kStreamPort),
          local_ip(0) {}
  };

  // name: shown to the other console while connecting.
  static WifiTransport *CreateHost(const std::string &name,
                                   const Options &options);
  static WifiTransport *CreateJoin(const std::string &name,
                                   const Options &options);
  ~WifiTransport();

  void Poll(uint64_t now_ms) override;
  State GetState() const override { return state_; }
  void Send(const std::string &bytes) override { outbox_ += bytes; }
  bool SendQueueEmpty() const override { return outbox_.empty(); }
  size_t QueuedBytes() const override { return outbox_.size(); }
  std::string TakeReceived() override;
  std::string Error() const override { return error_; }
  std::string PeerName() const override { return peer_name_; }
  void Close() override;

private:
  WifiTransport(bool host, const std::string &name, const Options &options);
  bool StartHost();
  bool StartJoin();
  void PollHostWaiting();
  void PollJoinWaiting(uint64_t now_ms);
  void PollStream();
  void Fail(const char *what);
  void FailWithErrno(const char *what);

  bool host_;
  std::string name_;
  Options options_;
  State state_;
  int udp_fd_;
  int listen_fd_;
  int stream_fd_;
  uint64_t next_discovery_ms_;
  std::string outbox_;
  std::string inbox_;
  std::string error_;
  std::string peer_name_;
};
