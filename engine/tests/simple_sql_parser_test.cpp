#include "antsql/simple_sql_parser.hpp"

#include <cassert>

namespace {
using namespace antsql;

SimpleSqlParser MakeParser() {
  return SimpleSqlParser({{"readings", "site_id"}, {"orders", "customer_id"}});
}

void ParsesSelectWithExactKeyPredicateTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT * FROM readings WHERE site_id = 7");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(statement->kind == QueryKind::Select);
  assert(statement->table == "readings");
  assert(statement->partition == 7);
  assert(statement->is_decomposable_aggregate == false);
}

void ParsesCaseInsensitivelyAndWithSemicolonTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("select * FROM Readings WHERE Site_Id = 42;");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(statement->table == "Readings");
  assert(statement->partition == 42);
}

void ParsesDecomposableAggregateWithoutPartitionTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT count(*) FROM readings");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(statement->kind == QueryKind::Select);
  assert(!statement->partition.has_value());
  assert(statement->is_decomposable_aggregate == true);
}

void PredicateOnNonKeyColumnLeavesPartitionUnsetTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT * FROM readings WHERE region = 3");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(!statement->partition.has_value());
}

void ParsesInsertWithKeyInColumnListTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("INSERT INTO readings (site_id, value) VALUES (9, 'ok')");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(statement->kind == QueryKind::Insert);
  assert(statement->partition == 9);
}

void ParsesInsertMissingKeyColumnAsUnpartitionedTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("INSERT INTO readings (value) VALUES (5)");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(!statement->partition.has_value());
}

void ParsesUpdateAndDeleteWithKeyPredicateTests() {
  const auto parser = MakeParser();
  const auto update = parser.Parse("UPDATE readings SET value = 3 WHERE site_id = 2");
  const auto* update_statement = std::get_if<ParsedStatement>(&update);
  assert(update_statement != nullptr && update_statement->partition == 2);

  const auto del = parser.Parse("DELETE FROM readings WHERE site_id = 2");
  const auto* delete_statement = std::get_if<ParsedStatement>(&del);
  assert(delete_statement != nullptr && delete_statement->partition == 2);
}

void UnknownLeadingKeywordIsUnsupportedNotAnErrorTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("CREATE TABLE t (a int)");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(statement->kind == QueryKind::Unsupported);
}

void UnknownTableIsAParseErrorTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT * FROM nonexistent WHERE id = 1");
  assert(std::holds_alternative<std::string>(result));
}

void MultiplePredicatesAreRejectedTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT * FROM readings WHERE site_id = 1 AND value = 2");
  assert(std::holds_alternative<std::string>(result));
}

void MalformedSqlIsAParseErrorTests() {
  const auto parser = MakeParser();
  assert(std::holds_alternative<std::string>(parser.Parse("SELECT * readings")));
  assert(std::holds_alternative<std::string>(parser.Parse("SELECT * FROM readings WHERE site_id 7")));
  assert(std::holds_alternative<std::string>(parser.Parse("SELECT * FROM readings WHERE site_id = 'unterminated")));
}

// A string literal against the key column is syntactically valid SQL, not a
// parse error — it just can't produce a partition, so SqlValidator rejects
// it the same uniform way it rejects a query with no WHERE clause at all.
void StringLiteralOnKeyColumnParsesWithoutPartitionTests() {
  const auto parser = MakeParser();
  const auto result = parser.Parse("SELECT * FROM readings WHERE site_id = 'x'");
  const auto* statement = std::get_if<ParsedStatement>(&result);
  assert(statement != nullptr);
  assert(!statement->partition.has_value());
}

struct SimpleSqlParserTestRegistration {
  SimpleSqlParserTestRegistration() {
    ParsesSelectWithExactKeyPredicateTests();
    ParsesCaseInsensitivelyAndWithSemicolonTests();
    ParsesDecomposableAggregateWithoutPartitionTests();
    PredicateOnNonKeyColumnLeavesPartitionUnsetTests();
    ParsesInsertWithKeyInColumnListTests();
    ParsesInsertMissingKeyColumnAsUnpartitionedTests();
    ParsesUpdateAndDeleteWithKeyPredicateTests();
    UnknownLeadingKeywordIsUnsupportedNotAnErrorTests();
    UnknownTableIsAParseErrorTests();
    MultiplePredicatesAreRejectedTests();
    MalformedSqlIsAParseErrorTests();
    StringLiteralOnKeyColumnParsesWithoutPartitionTests();
  }
} simple_sql_parser_test_registration;
}  // namespace
