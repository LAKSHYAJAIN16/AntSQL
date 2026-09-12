#include "antsql/tcp_forwarder.hpp"
#include "antsql/in_memory_topology.hpp"

#include <cassert>
#include <memory>

namespace {
using namespace antsql;

struct Executor final : IShardExecutor {
  int calls{};
  QueryResponse Execute(const QueryRequest& request) override {
    ++calls;
    return {QueryStatus::Ok, "executed on " + request.route.back(), 3.0};
  }
};

struct NullForwarder final : IForwarder {
  QueryResponse Forward(const std::string& neighbor, const QueryRequest&) override {
    return {QueryStatus::ShardUnavailable, "no further hops from: " + neighbor, 0.0};
  }
};

// Real TCP sockets over 127.0.0.1, not the in-process forwarder: this
// exercises wire::Encode/Decode and actual send()/recv() framing, not just
// the routing contract already covered by in_process_forwarder_test.cpp.
void TcpForwarderRoundTripsOverRealSocketTests() {
  Router owner_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology owner_topology;
  owner_topology.SetOwnedRoutes({{"readings", 7}});
  Executor owner_executor;
  NullForwarder owner_forwarder;
  Gateway owner("owner", owner_router, owner_topology, owner_executor, owner_forwarder);

  TcpServer server("127.0.0.1", /*port=*/0,
                    [&owner](const QueryRequest& request) { return owner.Execute(request); });
  server.Start();

  Router source_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology source_topology;
  source_topology.SetNeighbors({{"owner", 1.0}});
  Executor source_executor;
  TcpForwarder forwarder;
  forwarder.AddPeer("owner", "127.0.0.1", server.BoundPort());
  Gateway source("source", source_router, source_topology, source_executor, forwarder);

  QueryRequest request{"SELECT * FROM readings WHERE site_id = 7",
                       {QueryKind::Select, "readings", 7, false}, {}, 16};
  const auto response = source.Execute(request);
  assert(response.status == QueryStatus::Ok);
  // Executor reports request.route.back(), the forwarding gateway, not the
  // owner itself — same convention as in_process_forwarder_test.cpp.
  assert(response.payload == "executed on source");
  assert(source_executor.calls == 0 && owner_executor.calls == 1);
  assert(source_router.Pheromone({"readings", 7}, "owner") > 0.5);

  server.Stop();
  const auto after_stop = source.Execute(request);
  assert(after_stop.status == QueryStatus::ShardUnavailable);
}

void TcpForwarderReportsUnknownPeerTests() {
  TcpForwarder forwarder;
  QueryRequest request{"SELECT 1", {QueryKind::Select, "t", 0, false}, {"source"}, 1};
  const auto response = forwarder.Forward("nowhere", request);
  assert(response.status == QueryStatus::ShardUnavailable);
}

// TcpServer now serves connections concurrently (one thread per connection)
// specifically so TcpForwarder can keep one socket per neighbor open across
// calls instead of paying a fresh handshake every time. Verify the reuse
// actually happens: five round trips should land on one accepted
// connection at the server, not five.
void TcpForwarderReusesConnectionAcrossCallsTests() {
  Router owner_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology owner_topology;
  owner_topology.SetOwnedRoutes({{"orders", 3}});
  Executor owner_executor;
  NullForwarder owner_forwarder;
  Gateway owner("owner", owner_router, owner_topology, owner_executor, owner_forwarder);

  TcpServer server("127.0.0.1", /*port=*/0,
                    [&owner](const QueryRequest& request) { return owner.Execute(request); });
  server.Start();

  Router source_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology source_topology;
  source_topology.SetNeighbors({{"owner", 1.0}});
  Executor source_executor;
  TcpForwarder forwarder;
  forwarder.AddPeer("owner", "127.0.0.1", server.BoundPort());
  Gateway source("source", source_router, source_topology, source_executor, forwarder);

  QueryRequest request{"SELECT * FROM orders WHERE site_id = 3",
                       {QueryKind::Select, "orders", 3, false}, {}, 16};
  for (int i = 0; i < 5; ++i) {
    const auto response = source.Execute(request);
    assert(response.status == QueryStatus::Ok);
  }
  assert(owner_executor.calls == 5);
  assert(server.ConnectionsAccepted() == 1);

  server.Stop();
}

// A dead peer (server restarted, connection dropped) shouldn't wedge the
// forwarder: the stale cached socket should be discarded and one fresh
// connection attempted before reporting the neighbor unavailable.
void TcpForwarderRecoversFromStaleCachedConnectionTests() {
  Router owner_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology owner_topology;
  owner_topology.SetOwnedRoutes({{"orders", 5}});
  Executor owner_executor;
  NullForwarder owner_forwarder;
  Gateway owner("owner", owner_router, owner_topology, owner_executor, owner_forwarder);

  auto server = std::make_unique<TcpServer>(
      "127.0.0.1", /*port=*/0, [&owner](const QueryRequest& request) { return owner.Execute(request); });
  server->Start();
  const auto port = server->BoundPort();

  Router source_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology source_topology;
  source_topology.SetNeighbors({{"owner", 1.0}});
  Executor source_executor;
  TcpForwarder forwarder;
  forwarder.AddPeer("owner", "127.0.0.1", port);
  Gateway source("source", source_router, source_topology, source_executor, forwarder);

  QueryRequest request{"SELECT * FROM orders WHERE site_id = 5",
                       {QueryKind::Select, "orders", 5, false}, {}, 16};
  assert(source.Execute(request).status == QueryStatus::Ok);

  server->Stop();
  server.reset();
  // Same bound port, fresh listener: exercises the retry-on-stale path
  // rather than just "peer unreachable."
  auto revived = std::make_unique<TcpServer>(
      "127.0.0.1", port, [&owner](const QueryRequest& request) { return owner.Execute(request); });
  revived->Start();
  assert(source.Execute(request).status == QueryStatus::Ok);
  assert(owner_executor.calls == 2);
  revived->Stop();
}

struct TcpForwarderTestRegistration {
  TcpForwarderTestRegistration() {
    TcpForwarderRoundTripsOverRealSocketTests();
    TcpForwarderReportsUnknownPeerTests();
    TcpForwarderReusesConnectionAcrossCallsTests();
    TcpForwarderRecoversFromStaleCachedConnectionTests();
  }
} tcp_forwarder_test_registration;
}  // namespace
