#include "antsql/sql.hpp"

#include <cassert>

namespace {
using namespace antsql;

struct Parser final : ISqlParser {
  std::variant<ParsedStatement, std::string> result;
  std::variant<ParsedStatement, std::string> Parse(const std::string&) const override { return result; }
};

void SqlTests() {
  Parser parser;
  parser.result = ParsedStatement{QueryKind::Select, "readings", 7, false};
  SqlValidator validator(parser);
  const auto valid = validator.Validate("SELECT * FROM readings WHERE site_id = 7");
  assert(std::holds_alternative<ValidatedQuery>(valid));

  parser.result = ParsedStatement{QueryKind::Select, "readings", std::nullopt, false};
  const auto scatter = validator.Validate("SELECT count(*) FROM readings");
  assert(std::get<QueryResponse>(scatter).status == QueryStatus::UnsupportedSql);

  parser.result = std::string{"syntax error at SELECT"};
  const auto invalid = validator.Validate("SELEC");
  assert(std::get<QueryResponse>(invalid).status == QueryStatus::UnsupportedSql);
}
}  // namespace

struct SqlTestRegistration { SqlTestRegistration() { SqlTests(); } } sql_test_registration;
