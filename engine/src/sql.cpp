#include "antsql/sql.hpp"

namespace antsql {

std::variant<ValidatedQuery, QueryResponse> SqlValidator::Validate(const std::string& sql) const {
  const auto parsed = parser_.Parse(sql);
  if (const auto* error = std::get_if<std::string>(&parsed)) {
    return QueryResponse{QueryStatus::UnsupportedSql, *error, 0.0};
  }

  const auto& statement = std::get<ParsedStatement>(parsed);
  if (statement.kind == QueryKind::Unsupported || statement.table.empty()) {
    return QueryResponse{QueryStatus::UnsupportedSql, "unsupported SQL statement", 0.0};
  }
  if (!statement.partition.has_value()) {
    // Scatter/gather execution is deliberately not enabled until its merge
    // layer is implemented and tested against PostgreSQL result semantics.
    return QueryResponse{QueryStatus::UnsupportedSql,
                         "v1 requires an exact distribution-key predicate", 0.0};
  }
  return ValidatedQuery{statement, sql};
}

}  // namespace antsql
