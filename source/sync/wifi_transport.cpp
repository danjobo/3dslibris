#include "sync/wifi_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace {

static const char kDiscoverMsg[] = "3LSY?";
static const char kAnnounceMsg[] = "3LSY!";
static const uint64_t kDiscoveryIntervalMs = 500;
// Bounded work per Poll() so a frame never stalls.
static const int kMaxRecvPerPoll = 8;
static const size_t kRecvChunk = 16 * 1024;
static const size_t kSendChunk = 16 * 1024;

void CloseFd(int *fd) {
  if (*fd < 0)
    return;
#ifdef __3DS__
  closesocket(*fd);
#else
  close(*fd);
#endif
  *fd = -1;
}

bool SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool WouldBlock() {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS ||
         errno == EALREADY;
}

sockaddr_in MakeAddr(uint32_t ip_host_order, uint16_t port) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip_host_order);
  return addr;
}

int OpenBoundSocket(int type, uint16_t port) {
  const int fd = socket(AF_INET, type, 0);
  if (fd < 0)
    return -1;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr = MakeAddr(INADDR_ANY, port);
  if (bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0 || !SetNonBlocking(fd)) {
    int tmp = fd;
    CloseFd(&tmp);
    return -1;
  }
  return fd;
}

} // namespace

WifiTransport::WifiTransport(bool host, const std::string &name,
                             const Options &options)
    : host_(host), name_(name), options_(options), state_(kWaiting),
      udp_fd_(-1), listen_fd_(-1), stream_fd_(-1), connecting_(false),
      next_discovery_ms_(0) {}

WifiTransport::~WifiTransport() { Close(); }

WifiTransport *WifiTransport::CreateHost(const std::string &name,
                                         const Options &options) {
  WifiTransport *t = new WifiTransport(true, name, options);
  t->StartHost();
  return t;
}

WifiTransport *WifiTransport::CreateJoin(const std::string &name,
                                         const Options &options) {
  WifiTransport *t = new WifiTransport(false, name, options);
  t->StartJoin();
  return t;
}

void WifiTransport::Fail(const char *what) {
  if (state_ == kFailed)
    return;
  error_ = what;
  state_ = kFailed;
  CloseFd(&udp_fd_);
  CloseFd(&listen_fd_);
  CloseFd(&stream_fd_);
}

bool WifiTransport::StartHost() {
  udp_fd_ = OpenBoundSocket(SOCK_DGRAM, options_.discovery_port);
  listen_fd_ = OpenBoundSocket(SOCK_STREAM, options_.stream_port);
  if (udp_fd_ < 0 || listen_fd_ < 0 || listen(listen_fd_, 1) != 0) {
    Fail("Couldn't open the network ports");
    return false;
  }
  return true;
}

bool WifiTransport::StartJoin() {
  udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd_ < 0 || !SetNonBlocking(udp_fd_)) {
    Fail("Couldn't open a network socket");
    return false;
  }
  int yes = 1;
  setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  return true;
}

void WifiTransport::Poll(uint64_t now_ms) {
  if (state_ == kFailed || state_ == kClosed)
    return;
  if (state_ == kWaiting) {
    if (host_)
      PollHostWaiting();
    else if (connecting_)
      PollConnecting();
    else
      PollJoinWaiting(now_ms);
  }
  if (state_ == kConnected)
    PollStream();
}

void WifiTransport::PollHostWaiting() {
  // Answer discovery broadcasts with our name.
  char buf[64];
  sockaddr_in from;
  socklen_t from_len = sizeof(from);
  const int n = (int)recvfrom(udp_fd_, buf, sizeof(buf), 0, (sockaddr *)&from,
                              &from_len);
  if (n >= (int)sizeof(kDiscoverMsg) - 1 &&
      memcmp(buf, kDiscoverMsg, sizeof(kDiscoverMsg) - 1) == 0) {
    const std::string reply = std::string(kAnnounceMsg) + name_;
    sendto(udp_fd_, reply.data(), reply.size(), 0, (sockaddr *)&from,
           from_len);
  }

  sockaddr_in peer;
  socklen_t peer_len = sizeof(peer);
  const int fd = accept(listen_fd_, (sockaddr *)&peer, &peer_len);
  if (fd < 0) {
    if (!WouldBlock())
      Fail("Waiting for the other 3DS failed");
    return;
  }
  if (!SetNonBlocking(fd)) {
    int tmp = fd;
    CloseFd(&tmp);
    return;
  }
  stream_fd_ = fd;
  CloseFd(&listen_fd_);
  CloseFd(&udp_fd_);
  state_ = kConnected;
}

void WifiTransport::PollJoinWaiting(uint64_t now_ms) {
  if (now_ms >= next_discovery_ms_) {
    next_discovery_ms_ = now_ms + kDiscoveryIntervalMs;
    const std::string msg = std::string(kDiscoverMsg) + name_;
    if (!options_.direct_host.empty()) {
      sockaddr_in to = MakeAddr(0, options_.discovery_port);
      inet_pton(AF_INET, options_.direct_host.c_str(), &to.sin_addr);
      sendto(udp_fd_, msg.data(), msg.size(), 0, (sockaddr *)&to, sizeof(to));
    } else {
      sockaddr_in to = MakeAddr(0xFFFFFFFFu, options_.discovery_port);
      sendto(udp_fd_, msg.data(), msg.size(), 0, (sockaddr *)&to, sizeof(to));
      if (options_.local_ip) {
        // Some access points drop 255.255.255.255; try the /24 broadcast.
        to = MakeAddr(options_.local_ip | 0xFFu, options_.discovery_port);
        sendto(udp_fd_, msg.data(), msg.size(), 0, (sockaddr *)&to,
               sizeof(to));
      }
    }
  }

  char buf[96];
  sockaddr_in from;
  socklen_t from_len = sizeof(from);
  const int n = (int)recvfrom(udp_fd_, buf, sizeof(buf), 0, (sockaddr *)&from,
                              &from_len);
  if (n < (int)sizeof(kAnnounceMsg) - 1 ||
      memcmp(buf, kAnnounceMsg, sizeof(kAnnounceMsg) - 1) != 0)
    return;
  peer_name_.assign(buf + sizeof(kAnnounceMsg) - 1,
                    (size_t)n - (sizeof(kAnnounceMsg) - 1));

  stream_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (stream_fd_ < 0 || !SetNonBlocking(stream_fd_)) {
    Fail("Couldn't open a connection");
    return;
  }
  sockaddr_in host = from;
  host.sin_port = htons(options_.stream_port);
  if (connect(stream_fd_, (sockaddr *)&host, sizeof(host)) == 0) {
    CloseFd(&udp_fd_);
    state_ = kConnected;
    return;
  }
  if (!WouldBlock()) {
    Fail("Couldn't connect to the other 3DS");
    return;
  }
  connecting_ = true;
}

void WifiTransport::PollConnecting() {
  pollfd p;
  p.fd = stream_fd_;
  p.events = POLLOUT;
  p.revents = 0;
  if (poll(&p, 1, 0) <= 0)
    return;
  int err = 0;
  socklen_t len = sizeof(err);
  getsockopt(stream_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
  if (err != 0 || (p.revents & (POLLERR | POLLHUP))) {
    Fail("Couldn't connect to the other 3DS");
    return;
  }
  connecting_ = false;
  CloseFd(&udp_fd_);
  state_ = kConnected;
}

void WifiTransport::PollStream() {
  while (!outbox_.empty()) {
    const size_t len = outbox_.size() < kSendChunk ? outbox_.size() : kSendChunk;
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL; // a closed peer must not kill the process
#else
    const int flags = 0;
#endif
    const int n = (int)send(stream_fd_, outbox_.data(), len, flags);
    if (n > 0) {
      outbox_.erase(0, (size_t)n);
      continue;
    }
    if (n < 0 && WouldBlock())
      break;
    Fail("Connection lost");
    return;
  }

  char buf[kRecvChunk];
  for (int i = 0; i < kMaxRecvPerPoll; i++) {
    const int n = (int)recv(stream_fd_, buf, sizeof(buf), 0);
    if (n > 0) {
      inbox_.append(buf, (size_t)n);
      continue;
    }
    if (n == 0) {
      state_ = kClosed;
      CloseFd(&stream_fd_);
      return;
    }
    if (WouldBlock())
      break;
    Fail("Connection lost");
    return;
  }
}

std::string WifiTransport::TakeReceived() {
  std::string out;
  out.swap(inbox_);
  return out;
}

void WifiTransport::Close() {
  CloseFd(&udp_fd_);
  CloseFd(&listen_fd_);
  CloseFd(&stream_fd_);
  if (state_ != kFailed)
    state_ = kClosed;
}
