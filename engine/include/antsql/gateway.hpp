#pragma once

#include "antsql/router.hpp"

#include <optional>
#include <string>
#include <vector>

namespace antsql {

enum class QueryKind { Select, Insert, Update, Delete, Unsupported };

struct ParsedStatement {
  QueryKind kind{QueryKind::Unsupported};
  std::string table;
  std::optional<std::uint32_t> partition;
  bool is_decomposable_aggregate{};
};

struct QueryRequest {
  std::string sql;
  ParsedStatement statement;
  std::vector<std::string> route;
  std::uint32_t hop_budget{16};
};

enum class QueryStatus { Ok, UnsupportedSql, RouteExhausted, ShardUnavailable, ExecutionError };

struct QueryResponse {
  QueryStatus status{QueryStatus::ExecutionError};
  std::string payload;
  double elapsed_ms{};
};

class IShardExecutor {
 public:
  virtual ~IShardExecutor() = default;
  virtual QueryResponse Execute(const QueryRequest& request) = 0;
};

class IForwarder {
 public:
  virtual ~IForwarder() = default;
  virtual QueryResponse Forward(const std::string& neighbor, const QueryRequest& request) = 0;
};

class ITopology {
 public:
  virtual ~ITopology() = default;
  virtual bool Owns(const RouteKey& key) const = 0;
  virtual std::vector<Neighbor> Neighbors() const = 0;
};

class Gateway {
 public:
  Gateway(std::string node_id, Router& router, ITopology& topology,
          IShardExecutor& shard_executor, IForwarder& forwarder);

  QueryResponse Execute(QueryRequest request);

 private:
  std::string node_id_;
  Router& router_;
  ITopology& topology_;
  IShardExecutor& shard_executor_;
  IForwarder& forwarder_;
};

}  // namespace antsql
