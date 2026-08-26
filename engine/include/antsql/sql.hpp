#pragma once

#include "antsql/gateway.hpp"

#include <string>
#include <variant>

namespace antsql {

// Production implementations adapt a PostgreSQL-compatible parser such as
// libpg_query. Keeping parsing behind this interface prevents the gateway
// from making unsafe routing decisions using string matching or regexes.
class ISqlParser {
 public:
  virtual ~ISqlParser() = default;
  virtual std::variant<ParsedStatement, std::string> Parse(const std::string& sql) const = 0;
};

struct ValidatedQuery {
  ParsedStatement statement;
  std::string sql;
};

class SqlValidator {
 public:
  explicit SqlValidator(const ISqlParser& parser) : parser_(parser) {}
  std::variant<ValidatedQuery, QueryResponse> Validate(const std::string& sql) const;

 private:
  const ISqlParser& parser_;
};

}  // namespace antsql
