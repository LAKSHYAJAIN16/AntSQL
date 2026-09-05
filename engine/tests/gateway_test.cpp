#include "antsql/gateway.hpp"

#include <cassert>

namespace {
using namespace antsql;

struct Topology final : ITopology {
  bool owns{false};
  bool Owns(const RouteKey&) const override { return owns; }
  std::vector<Neighbor> Neighbors() const override { return {{"next", 1.0}}; }
};
struct Executor final : IShardExecutor {
  int calls{};
  QueryResponse Execute(const QueryRequest&) override { ++calls; return {QueryStatus::Ok, "local", 2.0}; }
};
struct Forwarder final : IForwarder {
  int calls{};
  QueryResponse Forward(const std::string& neighbor, const QueryRequest&) override {
    ++calls; assert(neighbor == "next"); return {QueryStatus::Ok, "remote", 4.0};
  }
};

void GatewayTests() {
  Router router(RouterConfig{.exploration_probability = 0.0});
  Topology topology;
  Executor executor;
  Forwarder forwarder;
  Gateway gateway("source", router, topology, executor, forwarder);
  QueryRequest request{"SELECT * FROM readings WHERE site_id = 7",
                       {QueryKind::Select, "readings", 7, false}, {}, 16};

  const auto remote = gateway.Execute(request);
  assert(remote.status == QueryStatus::Ok && forwarder.calls == 1 && executor.calls == 0);

  topology.owns = true;
  const auto local = gateway.Execute(request);
  assert(local.status == QueryStatus::Ok && executor.calls == 1);

  request.statement.partition.reset();
  assert(gateway.Execute(request).status == QueryStatus::UnsupportedSql);
}
}  // namespace

int main();
struct GatewayTestRegistration { GatewayTestRegistration() { GatewayTests(); } } gateway_test_registration;
