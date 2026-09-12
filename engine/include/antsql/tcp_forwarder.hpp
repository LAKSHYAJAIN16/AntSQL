#pragma once

#include "antsql/gateway.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace antsql {

// Dependency-free TCP transport for the native multi-gateway harness. Frames
// one or more request/response pairs per connection (TcpForwarder reuses a
// cached connection across calls to the same neighbor). An
// Arrow-Flight/gRPC-based IForwarder can replace this later without
// changing Gateway or the routing contract, since both depend only on
// IForwarder.
class TcpServer {
 public:
  // handler may run concurrently from multiple connection threads; keep it
  // thread-safe (Router, InMemoryTopology, and Gateway itself already are).
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

  // Total connections accepted over this server's lifetime, including ones
  // already closed. Test instrumentation for verifying connection reuse.
  int ConnectionsAccepted() const { return connections_accepted_; }

 private:
  void AcceptLoop();
  // Serves one connection until the peer disconnects, a frame error occurs,
  // or Stop() closes the socket out from under it; runs on its own thread
  // so a peer holding a connection open (for reuse) doesn't stall others.
  void HandleConnection(std::intptr_t client_socket);

  std::string bind_host_;
  std::uint16_t requested_port_;
  std::uint16_t bound_port_{0};
  std::function<QueryResponse(const QueryRequest&)> handler_;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  std::atomic<int> connections_accepted_{0};
  // Platform socket handle, stored as an integer so <winsock2.h>/<sys/socket.h>
  // stay out of this public header.
  std::intptr_t listen_socket_{-1};

  // One worker thread per connection. Threads accumulate here until Stop()
  // joins them all; fine at this harness's connection counts, not a design
  // meant for long-lived, high-connection-churn deployments.
  std::mutex threads_mutex_;
  std::vector<std::thread> connection_threads_;
  std::mutex connections_mutex_;
  std::vector<std::intptr_t> open_connections_;
};

class TcpForwarder final : public IForwarder {
 public:
  ~TcpForwarder() override;

  void AddPeer(std::string node_id, std::string host, std::uint16_t port);
  QueryResponse Forward(const std::string& neighbor, const QueryRequest& request) override;

 private:
  struct Peer {
    std::string host;
    std::uint16_t port;
  };
  // Defined in the .cpp (holds a platform socket handle); declared here
  // only so it can be named in unique_ptr<Connection>.
  struct Connection;

  QueryResponse ExchangeOverConnection(Connection& connection, const Peer& peer,
                                       const QueryRequest& request, const std::string& neighbor);

  std::mutex mutex_;
  std::unordered_map<std::string, Peer> peers_;
  // One cached, reusable TCP connection per neighbor, avoiding a fresh
  // handshake on every Forward() call. Each Connection has its own mutex so
  // concurrent Forward() calls to different neighbors don't block each
  // other; concurrent calls to the *same* neighbor are serialized, since
  // this transport doesn't pipeline requests on one socket.
  // Raw pointer, not unique_ptr: unordered_map's value type isn't
  // guaranteed to support an incomplete type the way vector's is, and
  // Connection is still incomplete here (defined in the .cpp); owned and
  // deleted explicitly in ~TcpForwarder().
  std::unordered_map<std::string, Connection*> connections_;
};

}  // namespace antsql
