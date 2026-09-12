#include "antsql/tcp_forwarder.hpp"
#include "antsql/wire.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace antsql {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void CloseSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void CloseSocket(SocketHandle socket) { close(socket); }
#endif

// A frame this large would already be well beyond any real QueryRequest/
// QueryResponse; rejecting it up front caps the allocation a malformed or
// hostile 4-byte length prefix can force.
constexpr std::uint32_t kMaxFrameBytes = 64u * 1024u * 1024u;

#ifdef _WIN32
// WSAStartup/WSACleanup are process-wide, so this is initialized exactly
// once (C++11 magic statics) no matter how many TcpServer/TcpForwarder
// instances exist, and torn down at process exit.
class WinsockGuard {
 public:
  WinsockGuard() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  }
  ~WinsockGuard() { WSACleanup(); }
};
#endif

void EnsureNetworkInit() {
#ifdef _WIN32
  static WinsockGuard guard;
  (void)guard;
#endif
}

bool SendAll(SocketHandle socket, const char* data, std::size_t length) {
  std::size_t sent = 0;
  while (sent < length) {
    const auto result = send(socket, data + sent, static_cast<int>(length - sent), 0);
    if (result <= 0) return false;
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

bool RecvExact(SocketHandle socket, char* buffer, std::size_t length) {
  std::size_t received = 0;
  while (received < length) {
    const auto result = recv(socket, buffer + received, static_cast<int>(length - received), 0);
    if (result <= 0) return false;
    received += static_cast<std::size_t>(result);
  }
  return true;
}

bool SendFrame(SocketHandle socket, const std::string& payload) {
  const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
  const char header[4] = {
      static_cast<char>((length >> 24) & 0xFF), static_cast<char>((length >> 16) & 0xFF),
      static_cast<char>((length >> 8) & 0xFF), static_cast<char>(length & 0xFF)};
  return SendAll(socket, header, sizeof(header)) && SendAll(socket, payload.data(), payload.size());
}

bool RecvFrame(SocketHandle socket, std::string& payload) {
  char header[4];
  if (!RecvExact(socket, header, sizeof(header))) return false;
  const auto length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[0])) << 24) |
                       (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[1])) << 16) |
                       (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[2])) << 8) |
                       static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[3]));
  if (length > kMaxFrameBytes) return false;
  payload.resize(length);
  if (length == 0) return true;
  return RecvExact(socket, payload.data(), length);
}

}  // namespace

TcpServer::TcpServer(std::string bind_host, std::uint16_t port,
                     std::function<QueryResponse(const QueryRequest&)> handler)
    : bind_host_(std::move(bind_host)), requested_port_(port), handler_(std::move(handler)) {}

TcpServer::~TcpServer() { Stop(); }

void TcpServer::Start() {
  EnsureNetworkInit();

  const auto listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket == kInvalidSocket) throw std::runtime_error("socket() failed");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(requested_port_);
  if (inet_pton(AF_INET, bind_host_.c_str(), &address.sin_addr) != 1) {
    CloseSocket(listen_socket);
    throw std::runtime_error("invalid bind address: " + bind_host_);
  }

  if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    CloseSocket(listen_socket);
    throw std::runtime_error("bind() failed");
  }
  if (listen(listen_socket, /*backlog=*/16) != 0) {
    CloseSocket(listen_socket);
    throw std::runtime_error("listen() failed");
  }

  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  if (getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
    bound_port_ = ntohs(bound.sin_port);
  } else {
    bound_port_ = requested_port_;
  }

  listen_socket_ = static_cast<std::intptr_t>(listen_socket);
  running_ = true;
  accept_thread_ = std::thread(&TcpServer::AcceptLoop, this);
}

void TcpServer::Stop() {
  if (!running_.exchange(false)) return;
  // Closing the listening socket unblocks the accept() call in AcceptLoop.
  CloseSocket(static_cast<SocketHandle>(listen_socket_));
  // Join the accept thread first: once it has returned, AcceptLoop can no
  // longer add to open_connections_/connection_threads_, so the two loops
  // below see the final, complete set with no race against new arrivals.
  if (accept_thread_.joinable()) accept_thread_.join();

  {
    std::scoped_lock lock(connections_mutex_);
    // Unblocks each connection thread's recv() so it can exit its loop and
    // be joined below, instead of waiting for the peer to close first.
    for (const auto client : open_connections_) {
      CloseSocket(static_cast<SocketHandle>(client));
    }
    open_connections_.clear();
  }
  {
    std::scoped_lock lock(threads_mutex_);
    for (auto& worker : connection_threads_) {
      if (worker.joinable()) worker.join();
    }
    connection_threads_.clear();
  }
}

void TcpServer::AcceptLoop() {
  const auto listen_socket = static_cast<SocketHandle>(listen_socket_);
  while (running_) {
    const auto client = accept(listen_socket, nullptr, nullptr);
    if (client == kInvalidSocket) {
      if (!running_) return;  // Stop() closed the listening socket.
      continue;
    }
    if (!running_) {
      // Accepted in the race window right as Stop() began; refuse it
      // rather than starting a connection thread Stop() won't wait for.
      CloseSocket(client);
      continue;
    }

    const auto client_handle = static_cast<std::intptr_t>(client);
    ++connections_accepted_;
    {
      std::scoped_lock lock(connections_mutex_);
      open_connections_.push_back(client_handle);
    }
    std::thread worker(&TcpServer::HandleConnection, this, client_handle);
    std::scoped_lock lock(threads_mutex_);
    connection_threads_.push_back(std::move(worker));
  }
}

void TcpServer::HandleConnection(std::intptr_t client_socket) {
  const auto client = static_cast<SocketHandle>(client_socket);
  while (running_) {
    std::string request_bytes;
    if (!RecvFrame(client, request_bytes)) break;  // peer closed, or Stop() did.
    const auto request = wire::DecodeRequest(request_bytes);
    const auto response = request ? handler_(*request)
                                   : QueryResponse{QueryStatus::ExecutionError,
                                                   "malformed request frame", 0.0};
    if (!SendFrame(client, wire::EncodeResponse(response))) break;
  }
  CloseSocket(client);

  std::scoped_lock lock(connections_mutex_);
  auto& connections = open_connections_;
  connections.erase(std::remove(connections.begin(), connections.end(), client_socket),
                    connections.end());
}

struct TcpForwarder::Connection {
  // Guards socket use for the lifetime of one Forward() call: this
  // transport doesn't pipeline requests, so concurrent forwards to the same
  // neighbor share the cached socket one at a time rather than racing on it.
  std::mutex mutex;
  SocketHandle socket{kInvalidSocket};
};

TcpForwarder::~TcpForwarder() {
  std::scoped_lock lock(mutex_);
  for (auto& [_, connection] : connections_) {
    if (connection->socket != kInvalidSocket) CloseSocket(connection->socket);
    delete connection;
  }
}

void TcpForwarder::AddPeer(std::string node_id, std::string host, std::uint16_t port) {
  std::scoped_lock lock(mutex_);
  peers_.insert_or_assign(std::move(node_id), Peer{std::move(host), port});
}

QueryResponse TcpForwarder::ExchangeOverConnection(Connection& connection, const Peer& peer,
                                                   const QueryRequest& request,
                                                   const std::string& neighbor) {
  if (connection.socket == kInvalidSocket) {
    connection.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection.socket == kInvalidSocket) {
      return {QueryStatus::ShardUnavailable, "socket() failed for neighbor: " + neighbor, 0.0};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(peer.port);
    const bool valid_address = inet_pton(AF_INET, peer.host.c_str(), &address.sin_addr) == 1;
    if (!valid_address ||
        connect(connection.socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      CloseSocket(connection.socket);
      connection.socket = kInvalidSocket;
      return {QueryStatus::ShardUnavailable, "unreachable neighbor: " + neighbor, 0.0};
    }
  }

  if (!SendFrame(connection.socket, wire::EncodeRequest(request))) {
    CloseSocket(connection.socket);
    connection.socket = kInvalidSocket;
    return {QueryStatus::ShardUnavailable, "unreachable neighbor: " + neighbor, 0.0};
  }

  std::string response_bytes;
  if (!RecvFrame(connection.socket, response_bytes)) {
    CloseSocket(connection.socket);
    connection.socket = kInvalidSocket;
    return {QueryStatus::ShardUnavailable, "unreachable neighbor: " + neighbor, 0.0};
  }

  if (const auto decoded = wire::DecodeResponse(response_bytes)) return *decoded;
  // Transport succeeded but the payload didn't decode; the connection
  // itself is still good, so keep it cached for reuse.
  return {QueryStatus::ExecutionError, "malformed response frame from: " + neighbor, 0.0};
}

QueryResponse TcpForwarder::Forward(const std::string& neighbor, const QueryRequest& request) {
  Peer peer;
  Connection* connection = nullptr;
  {
    std::scoped_lock lock(mutex_);
    const auto found = peers_.find(neighbor);
    if (found == peers_.end()) {
      return {QueryStatus::ShardUnavailable, "no known address for neighbor: " + neighbor, 0.0};
    }
    peer = found->second;
    auto& slot = connections_[neighbor];
    if (!slot) slot = new Connection();
    connection = slot;
  }

  EnsureNetworkInit();
  std::scoped_lock connection_lock(connection->mutex);
  // A cached socket may have gone stale between calls (e.g. the peer's
  // TcpServer connection thread exited). If the *first* attempt fails and
  // it was using what looked like a live cached connection, retry once on
  // a fresh one before reporting the neighbor unavailable; a failure on an
  // already-fresh connection is a real unreachable-neighbor result.
  const bool had_cached_connection = connection->socket != kInvalidSocket;
  auto response = ExchangeOverConnection(*connection, peer, request, neighbor);
  if (response.status == QueryStatus::ShardUnavailable && had_cached_connection) {
    response = ExchangeOverConnection(*connection, peer, request, neighbor);
  }
  return response;
}

}  // namespace antsql
