#pragma once

#include "antsql/sql.hpp"

#include <string>
#include <unordered_map>

namespace antsql {

// A hand-written parser for AntSQL's deliberately narrow v1 SQL subset:
// single-table SELECT/INSERT/UPDATE/DELETE with at most one equality
// predicate against a configured integer distribution key. It is not a
// PostgreSQL-compatible parser (no joins, subqueries, expressions, or
// non-integer keys) — it exists so the gateway has a real ISqlParser
// implementation to route against before a PostgreSQL-grammar parser such
// as libpg_query is available in this environment. Swapping this out later
// requires no change to ISqlParser, SqlValidator, or Gateway.
class SimpleSqlParser final : public ISqlParser {
 public:
  // Maps each routable table name to the (case-insensitive) column that
  // carries its distribution key, e.g. {"readings", "site_id"}. A table not
  // present here is reported as an unknown-table parse error rather than
  // silently treated as unroutable.
  explicit SimpleSqlParser(std::unordered_map<std::string, std::string> distribution_key_by_table);

  std::variant<ParsedStatement, std::string> Parse(const std::string& sql) const override;

 private:
  std::unordered_map<std::string, std::string> distribution_key_by_table_;
};

}  // namespace antsql
