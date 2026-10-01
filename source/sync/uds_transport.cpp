/*
    3dslibris - uds_transport.cpp

    See include/sync/uds_transport.h.
*/

#include "sync/uds_transport.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "sync/sync_protocol.h"

namespace {

// Identifies 3dslibris networks among other local play games ("3LIB").
const u32 kCommId = 0x334C4942u;
// Bumped if the beacon or link format changes, so old versions don't match.
const u8 kNetworkId8 = 1;
const u8 kDataChannel = 1;
const size_t kSharedMemSize = 0x10000;
// Holds a full window of frames between two polls.
const u32 kRecvBufferSize = 0x8000;
const size_t kScanBufferSize = 0x4000;
const uint64_t kStatusIntervalMs = 500;
const int kMaxPacketsPerPoll = 64;
const s64 kScanPauseNs = 200 * 1000 * 1000LL;

const char kBeaconMagic[4] = {'3', 'L', 'S', 'Y'};
const size_t kBeaconNameMax = 32;

struct Beacon {
  char magic[4];
  u32 code_hash;
  char name[kBeaconNameMax];
};

u32 CodeHash(const std::string &code) {
  const std::string salted = "3dslibris-sync:" + code;
  return sync_protocol::Crc32(salted.data(), salted.size());
}

std::string Passphrase(const std::string &code) {
  return "3dslibris local sync " + code;
}

std::string ResultText(const char *what, Result rc) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s (error %08lX)", what, (unsigned long)rc);
  return buf;
}

} // namespace

UdsTransport::UdsTransport(Role role, const std::string &name,
                           const std::string &code)
    : role_(role), name_(name), code_(code), state_(kWaiting),
      uds_ready_(false), network_up_(false), peer_node_(0),
      last_status_ms_(0), join_thread_(NULL), join_stop_(false),
      join_done_(false), join_connected_(false) {
  memset(&bind_, 0, sizeof(bind_));
  LightLock_Init(&join_lock_);
}

UdsTransport *UdsTransport::CreateHost(const std::string &name,
                                       const std::string &pairing_code) {
  UdsTransport *t = new UdsTransport(kHost, name, pairing_code);
  t->Start();
  return t;
}

UdsTransport *UdsTransport::CreateJoin(const std::string &name,
                                       const std::string &pairing_code) {
  UdsTransport *t = new UdsTransport(kJoin, name, pairing_code);
  t->Start();
  return t;
}

UdsTransport::~UdsTransport() { Shutdown(); }

void UdsTransport::Fail(const std::string &message) {
  if (state_ == kFailed || state_ == kClosed)
    return;
  error_ = message;
  state_ = kFailed;
}

bool UdsTransport::Start() {
  Result rc = udsInit(kSharedMemSize, NULL);
  if (R_FAILED(rc)) {
    Fail(ResultText("Local wireless isn't available. Is wireless "
                    "communication turned on?",
                    rc));
    return false;
  }
  uds_ready_ = true;

  if (role_ == kJoin) {
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Lower priority than the main loop so drawing stays smooth.
    join_thread_ = threadCreate(&UdsTransport::JoinThreadMain, this, 0x4000,
                                priority + 1, -2, false);
    if (!join_thread_) {
      Fail("Couldn't start searching");
      return false;
    }
    return true;
  }

  udsNetworkStruct network;
  udsGenerateDefaultNetworkStruct(&network, kCommId, kNetworkId8, 2);
  const std::string pass = Passphrase(code_);
  rc = udsCreateNetwork(&network, pass.data(), pass.size(), &bind_,
                        kDataChannel, kRecvBufferSize);
  if (R_FAILED(rc)) {
    Fail(ResultText("Couldn't start a local wireless network. Is wireless "
                    "communication turned on?",
                    rc));
    return false;
  }
  network_up_ = true;

  Beacon beacon;
  memset(&beacon, 0, sizeof(beacon));
  memcpy(beacon.magic, kBeaconMagic, sizeof(beacon.magic));
  beacon.code_hash = CodeHash(code_);
  strncpy(beacon.name, name_.c_str(), kBeaconNameMax - 1);
  rc = udsSetApplicationData(&beacon, sizeof(beacon));
  if (R_FAILED(rc)) {
    Fail(ResultText("Couldn't announce this 3DS", rc));
    return false;
  }
  return true;
}

void UdsTransport::JoinThreadMain(void *arg) {
  static_cast<UdsTransport *>(arg)->RunJoinSearch();
}

// Worker thread: scan until a host with our code shows up, then connect.
void UdsTransport::RunJoinSearch() {
  void *scan_buf = malloc(kScanBufferSize);
  const u32 want_hash = CodeHash(code_);
  const std::string pass = Passphrase(code_);
  bool connected = false;
  std::string error;
  std::string host_name;
  int connect_failures = 0;

  while (scan_buf && !join_stop_ && !connected && connect_failures < 3) {
    udsNetworkScanInfo *networks = NULL;
    size_t total = 0;
    memset(scan_buf, 0, kScanBufferSize);
    Result rc = udsScanBeacons(scan_buf, kScanBufferSize, &networks, &total,
                               kCommId, kNetworkId8, NULL, false);
    if (R_SUCCEEDED(rc)) {
      for (size_t i = 0; i < total && !join_stop_ && !connected; i++) {
        Beacon beacon;
        size_t size = 0;
        memset(&beacon, 0, sizeof(beacon));
        if (R_FAILED(udsGetNetworkStructApplicationData(
                &networks[i].network, &beacon, sizeof(beacon), &size)) ||
            size < sizeof(beacon) ||
            memcmp(beacon.magic, kBeaconMagic, sizeof(beacon.magic)) != 0 ||
            beacon.code_hash != want_hash)
          continue; // another 3dslibris host, or another code
        beacon.name[kBeaconNameMax - 1] = '\0';
        rc = udsConnectNetwork(&networks[i].network, pass.data(), pass.size(),
                               &bind_, UDS_BROADCAST_NETWORKNODEID,
                               UDSCONTYPE_Client, kDataChannel,
                               kRecvBufferSize);
        if (R_SUCCEEDED(rc)) {
          connected = true;
          host_name = beacon.name;
        } else {
          connect_failures++;
          error = ResultText("Couldn't connect to the other 3DS", rc);
        }
      }
    }
    free(networks);
    if (!connected && !join_stop_)
      svcSleepThread(kScanPauseNs);
  }
  free(scan_buf);
  if (!scan_buf)
    error = "Out of memory";

  LightLock_Lock(&join_lock_);
  join_done_ = true;
  join_connected_ = connected;
  join_error_ = error;
  host_name_ = host_name;
  LightLock_Unlock(&join_lock_);
}

void UdsTransport::StopJoinThread() {
  if (!join_thread_)
    return;
  join_stop_ = true;
  // Waits for at most one scan or connect attempt.
  threadJoin(join_thread_, U64_MAX);
  threadFree(join_thread_);
  join_thread_ = NULL;
}

std::string UdsTransport::PeerName() const {
  LightLock_Lock(const_cast<LightLock *>(&join_lock_));
  const std::string name = host_name_;
  LightLock_Unlock(const_cast<LightLock *>(&join_lock_));
  return name;
}

void UdsTransport::CheckConnection(uint64_t now_ms) {
  if (last_status_ms_ != 0 && now_ms - last_status_ms_ < kStatusIntervalMs)
    return;
  last_status_ms_ = now_ms;
  udsConnectionStatus status;
  if (R_FAILED(udsGetConnectionStatus(&status)))
    return;
  if (state_ == kWaiting && role_ == kHost && status.total_nodes >= 2) {
    // The first client: the lowest node id other than the host's (bit 0).
    for (int bit = 1; bit < UDS_MAXNODES; bit++) {
      if (status.node_bitmask & (1u << bit)) {
        peer_node_ = (u16)(bit + 1);
        break;
      }
    }
    if (peer_node_) {
      udsSetNewConnectionsBlocked(true, true, false);
      state_ = kConnected;
    }
  } else if (state_ == kConnected && status.total_nodes < 2) {
    state_ = kClosed; // the other console left
  }
}

void UdsTransport::PumpPackets(uint64_t now_ms) {
  std::vector<char> buf(UDS_DATAFRAME_MAXSIZE);
  for (int i = 0; i < kMaxPacketsPerPoll; i++) {
    size_t size = 0;
    u16 src = 0;
    if (R_FAILED(udsPullPacket(&bind_, &buf[0], buf.size(), &size, &src)) ||
        size == 0)
      break;
    if (src == peer_node_)
      link_.OnDatagram(&buf[0], size);
  }
  std::vector<std::string> out;
  link_.CollectOutgoing(now_ms, &out);
  for (size_t i = 0; i < out.size(); i++) {
    const Result rc = udsSendTo(peer_node_, kDataChannel,
                                UDS_SENDFLAG_Default, out[i].data(),
                                out[i].size());
    // As UDS_CHECK_SENDTO_FATALERROR, without its signed/unsigned compare.
    if (R_FAILED(rc) && (u32)rc != 0xC86113F0u) {
      Fail(ResultText("Connection lost", rc));
      return;
    }
    // A non-fatal error drops the frame; the link sends it again.
  }
}

void UdsTransport::Poll(uint64_t now_ms) {
  if (state_ == kFailed || state_ == kClosed)
    return;
  if (role_ == kJoin && state_ == kWaiting) {
    LightLock_Lock(&join_lock_);
    const bool done = join_done_;
    const bool connected = join_connected_;
    const std::string error = join_error_;
    LightLock_Unlock(&join_lock_);
    if (!done)
      return;
    StopJoinThread();
    if (!connected) {
      Fail(error.empty() ? "Couldn't find the other 3DS" : error);
      return;
    }
    network_up_ = true;
    peer_node_ = UDS_HOST_NETWORKNODEID;
    state_ = kConnected;
  }
  CheckConnection(now_ms);
  if (state_ == kConnected)
    PumpPackets(now_ms);
}

void UdsTransport::Send(const std::string &bytes) { link_.Queue(bytes); }

std::string UdsTransport::TakeReceived() { return link_.TakeReceived(); }

void UdsTransport::Shutdown() {
  StopJoinThread();
  if (network_up_) {
    if (role_ == kHost)
      udsDestroyNetwork();
    else
      udsDisconnectNetwork();
    udsUnbind(&bind_);
    network_up_ = false;
  }
  if (uds_ready_) {
    udsExit();
    uds_ready_ = false;
  }
}

void UdsTransport::Close() {
  Shutdown();
  if (state_ != kFailed)
    state_ = kClosed;
}
