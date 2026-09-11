#include "antsql/tcp_forwarder.hpp"
#include "antsql/in_memory_topology.hpp"

#include <cassert>

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

struct TcpForwarderTestRegistration {
  TcpForwarderTestRegistration() {
    TcpForwarderRoundTripsOverRealSocketTests();
    TcpForwarderReportsUnknownPeerTests();
  }
} tcp_forwarder_test_registration;
}  // namespace
