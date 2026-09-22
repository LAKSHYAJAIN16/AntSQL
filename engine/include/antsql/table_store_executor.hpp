#pragma once

#include "antsql/gateway.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace antsql {

// A real, embedded storage engine: each table is a map from its integer
// distribution key to a row of named columns, held in memory and mirrored to
// an append-only write-ahead log so a node's data survives a restart. This is
// what turns a Gateway from a router sitting in front of someone else's
// database into an actual database node: the bytes a client reads back were
// written here, not proxied to PostgreSQL.
//
// It executes the same deliberately narrow SQL subset SimpleSqlParser
// accepts (single-table SELECT/INSERT/UPDATE/DELETE, at most one equality
// predicate on the distribution key) but needs its own small tokenizer
// because ParsedStatement intentionally carries only routing-relevant facts
// (table, partition) and not column/value data — SimpleSqlParser's job is to
// route safely, not to execute, and keeping those concerns apart is what
// lets either be swapped independently.
class TableStoreExecutor final : public IShardExecutor {
 public:
  // wal_path empty => memory-only (no cross-restart durability), used by
  // unit tests and short-lived demo nodes that don't need it.
  explicit TableStoreExecutor(std::string wal_path = {});

  QueryResponse Execute(const QueryRequest& request) override;

  // Introspection for dashboards/tests: how many rows this node currently
  // holds for a table, and a snapshot of one row (empty if absent).
  std::size_t RowCount(const std::string& table) const;
  std::string DumpRow(const std::string& table, std::uint32_t key) const;

 private:
  using Row = std::unordered_map<std::string, std::string>;
  using Table = std::map<std::uint32_t, Row>;

  QueryResponse ExecuteLocked(const QueryRequest& request);
  void ReplayWal();
  void AppendToWal(const QueryRequest& request);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Table> tables_;
  std::string wal_path_;
};

}  // namespace antsql
