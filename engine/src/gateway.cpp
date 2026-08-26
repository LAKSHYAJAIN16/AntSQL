#include "antsql/gateway.hpp"

#include <algorithm>
#include <unordered_set>

namespace antsql {

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
  if (topology_.Owns(key)) return shard_executor_.Execute(request);

  std::unordered_set<std::string> visited(request.route.begin(), request.route.end());
  visited.insert(node_id_);
  const auto decision = router_.Choose(key, topology_.Neighbors(), visited);
  if (!decision) return {QueryStatus::RouteExhausted, "no unvisited live neighbor", 0.0};

  request.route.push_back(node_id_);
  --request.hop_budget;
  auto response = forwarder_.Forward(decision->next_hop, request);
  if (response.status == QueryStatus::Ok) {
    auto observed_path = request.route;
    observed_path.push_back(decision->next_hop);
    router_.ObserveSuccess(key, observed_path, response.elapsed_ms);
  } else {
    router_.ObserveFailure(key, {node_id_, decision->next_hop});
  }
  return response;
}

}  // namespace antsql
