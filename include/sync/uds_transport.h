/*
    3dslibris - uds_transport.h

    Sync over local wireless (UDS, the 3DS "local play" network), for when
    there is no Wi-Fi router. The host creates a two-node network whose
    beacon carries its name and a hash of the pairing code; the joiner scans
    for a beacon with its code's hash and connects. The network passphrase
    includes the pairing code, so only a console with the code can join.

    "Nearby" mode needs no code: both consoles choose it, and each one
    alternates between searching for the other (a random 1.5-3 s) and
    hosting (a random 3-6 s) until one finds the other. The random timing
    keeps the two from staying in the same role. Any console nearby in this
    mode can connect, so it's for consoles side by side.

    Frames are small and can be lost, so the byte stream runs over a
    ReliableLink. Scanning and connecting block for a while, so the joiner
    does them on a worker thread; everything else is polled from the main
    loop. Device only.
*/

#pragma once

#include <3ds.h>
#include <stdint.h>
#include <string>

#include "sync/reliable_link.h"
#include "sync/sync_transport.h"

class UdsTransport : public SyncTransport {
public:
  // Both return a transport in kFailed (with Error()) when local wireless
  // can't start, e.g. when wireless communication is turned off.
  static UdsTransport *CreateHost(const std::string &name,
                                  const std::string &pairing_code);
  static UdsTransport *CreateJoin(const std::string &name,
                                  const std::string &pairing_code);
  // No code: pairs with another console in nearby mode (see above).
  static UdsTransport *CreateNearby(const std::string &name);
  // The pairing code both consoles use in nearby mode (for SyncSession).
  static const char *NearbyCode();
  ~UdsTransport();

  void Poll(uint64_t now_ms) override;
  State GetState() const override { return state_; }
  void Send(const std::string &bytes) override;
  bool SendQueueEmpty() const override { return link_.AllAcknowledged(); }
  size_t QueuedBytes() const override { return link_.QueuedBytes(); }
  std::string TakeReceived() override;
  std::string Error() const override { return error_; }
  std::string PeerName() const override;
  void Close() override;

private:
  enum Role { kHost, kJoin, kNearby };

  UdsTransport(Role role, const std::string &name, const std::string &code);
  bool Start();
  // Host role: create the network and announce it.
  bool StartHosting();
  bool StartJoinThread(uint64_t deadline_ms);
  void StopHosting();
  // Nearby mode: switch between searching and hosting.
  void PollNearby(uint64_t now_ms);
  uint64_t RandomMs(uint64_t lo, uint64_t hi);
  void Fail(const std::string &message);
  void PumpPackets(uint64_t now_ms);
  void CheckConnection(uint64_t now_ms);
  void StopJoinThread();
  void Shutdown();
  static void JoinThreadMain(void *arg);
  void RunJoinSearch();

  Role role_;
  std::string name_;
  std::string code_;
  State state_;
  std::string error_;
  bool uds_ready_;
  bool network_up_; // created (host) or connected (join)
  bool hosting_;    // this console created the network
  uint64_t nearby_switch_ms_; // nearby: when to stop hosting and search
  uint32_t random_state_;
  udsBindContext bind_;
  u16 peer_node_;
  ReliableLink link_;
  uint64_t last_status_ms_;

  // Joiner worker thread; the fields below join_lock_ are shared with it.
  Thread join_thread_;
  LightLock join_lock_;
  volatile bool join_stop_;
  uint64_t join_deadline_ms_; // 0 = search until found
  bool join_done_;
  bool join_connected_;
  std::string join_error_;
  std::string host_name_;
};
