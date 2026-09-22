#include "antsql/gateway.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace antsql {
namespace {

double ElapsedMs(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

Gateway::Gateway(std::string node_id, Router& router, ITopology& topology,
                 IShardExecutor& shard_executor, IForwarder& forwarder)
    : node_id_(std::move(node_id)), router_(router), topology_(topology),
      shard_executor_(shard_executor), forwarder_(forwarder) {}

QueryResponse Gateway::Execute(QueryRequest request) {
  if (request.statement.kind == QueryKind::Unsupported || request.statement.table.empty() ||
      !request.statement.partition.has_value()) {
    return {QueryStatus::UnsupportedSql,
            "v1 requires a PostgreSQL-parser-derived table and exact distribution key", 0.0};
  }
  if (request.hop_budget == 0) {
    return {QueryStatus::RouteExhausted, "route hop budget exhausted", 0.0};
  }

  const RouteKey key{request.statement.table, *request.statement.partition};
  if (topology_.Owns(key)) {
    // The real leaf cost of this query: actual local storage-engine time,
    // not a value some caller made up. Everything upstream's "total path
    // cost" is built out of measurements like this one, hop by hop.
    const auto start = std::chrono::steady_clock::now();
    auto response = shard_executor_.Execute(request);
    response.elapsed_ms = ElapsedMs(start);
    return response;
  }

  std::unordered_set<std::string> visited(request.route.begin(), request.route.end());
  visited.insert(node_id_);
  const auto decision = router_.Choose(key, topology_.Neighbors(), visited);
  if (!decision) return {QueryStatus::RouteExhausted, "no unvisited live neighbor", 0.0};

  request.route.push_back(node_id_);
  --request.hop_budget;
  const auto start = std::chrono::steady_clock::now();
  auto response = forwarder_.Forward(decision->next_hop, request);
  // Overwrite rather than add to whatever the downstream hop reported: a
  // blocking Forward() call's wall-clock time already covers every hop from
  // here to the owner, so re-measuring it locally at each gateway gives an
  // accurate end-to-end cost for *this* node's remaining path without
  // double-counting nested hops.
  response.elapsed_ms = ElapsedMs(start);
  if (response.status == QueryStatus::Ok) {
    // Router state is local to this gateway. Only reinforce the forwarding
    // edge selected here; a downstream gateway learns its own outgoing edge.
    router_.ObserveSuccess(key, {node_id_, decision->next_hop}, response.elapsed_ms);
  } else {
    router_.ObserveFailure(key, {node_id_, decision->next_hop});
  }
  return response;
}

}  // namespace antsql
