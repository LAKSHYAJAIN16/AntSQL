#pragma once

#include "antsql/gateway.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace antsql {

// Dependency-free TCP transport for the native multi-gateway harness. Frames
// one request/response pair per connection: simple and easy to reason
// about, at the cost of a fresh TCP handshake per hop. A pooled or
// Arrow-Flight/gRPC-based IForwarder can replace this later without
// changing Gateway or the routing contract, since both depend only on
// IForwarder.
class TcpServer {
 public:
  // handler runs on the accept thread, once per connection; keep it fast,
  // or dispatch internally, if concurrent request volume requires it.
  TcpServer(std::string bind_host, std::uint16_t port,
            std::function<QueryResponse(const QueryRequest&)> handler);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Binds and starts accepting; throws std::runtime_error on a socket
  // failure. Safe to call once.
  void Start();
  void Stop();

  // The actual bound port: equal to the requested port unless it was 0
  // (ephemeral), in which case this is the port the OS assigned. Valid only
  // after Start().
  std::uint16_t BoundPort() const { return bound_port_; }

 private:
  void AcceptLoop();

  std::string bind_host_;
  std::uint16_t requested_port_;
  std::uint16_t bound_port_{0};
  std::function<QueryResponse(const QueryRequest&)> handler_;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  // Platform socket handle, stored as an integer so <winsock2.h>/<sys/socket.h>
  // stay out of this public header.
  std::intptr_t listen_socket_{-1};
};

class TcpForwarder final : public IForwarder {
 public:
  void AddPeer(std::string node_id, std::string host, std::uint16_t port);
  QueryResponse Forward(const std::string& neighbor, const QueryRequest& request) override;

 private:
  struct Peer {
    std::string host;
    std::uint16_t port;
  };
  std::mutex mutex_;
  std::unordered_map<std::string, Peer> peers_;
};

}  // namespace antsql
