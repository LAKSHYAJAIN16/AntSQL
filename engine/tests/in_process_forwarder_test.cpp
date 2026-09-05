#include "antsql/in_process_forwarder.hpp"
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

void InProcessForwarderTests() {
  Router source_router(RouterConfig{.exploration_probability = 0.0});
  Router relay_router(RouterConfig{.exploration_probability = 0.0});
  Router owner_router(RouterConfig{.exploration_probability = 0.0});
  InMemoryTopology source_topology;
  source_topology.SetNeighbors({{"relay", 1.0}});
  InMemoryTopology relay_topology;
  relay_topology.SetNeighbors({{"owner", 1.0}});
  InMemoryTopology owner_topology;
  owner_topology.SetOwnedRoutes({{"readings", 7}});
  Executor source_executor;
  Executor relay_executor;
  Executor owner_executor;
  InProcessForwarder forwarder;
  Gateway source("source", source_router, source_topology, source_executor, forwarder);
  Gateway relay("relay", relay_router, relay_topology, relay_executor, forwarder);
  Gateway owner("owner", owner_router, owner_topology, owner_executor, forwarder);
  forwarder.Register("source", source);
  forwarder.Register("relay", relay);
  forwarder.Register("owner", owner);

  QueryRequest request{"SELECT * FROM readings WHERE site_id = 7",
                       {QueryKind::Select, "readings", 7, false}, {}, 16};
  const auto response = source.Execute(request);
  assert(response.status == QueryStatus::Ok);
  assert(response.payload == "executed on relay");
  assert(source_executor.calls == 0 && relay_executor.calls == 0 && owner_executor.calls == 1);
  assert(source_router.Pheromone({"readings", 7}, "relay") > 0.5);
  assert(relay_router.Pheromone({"readings", 7}, "owner") > 0.5);
  assert(relay_router.Pheromone({"readings", 7}, "relay") == 0.5);

  forwarder.Unregister("owner");
  const auto unavailable = source.Execute(request);
  assert(unavailable.status == QueryStatus::ShardUnavailable);
  assert(relay_router.Pheromone({"readings", 7}, "owner") < 0.5 + 2.5);
}

struct InProcessForwarderTestRegistration {
  InProcessForwarderTestRegistration() { InProcessForwarderTests(); }
} in_process_forwarder_test_registration;
}  // namespace
