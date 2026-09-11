#include "antsql/simple_sql_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <unordered_set>
#include <vector>

namespace antsql {
namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

enum class TokenType { Identifier, Number, String, Symbol, End };

struct Token {
  TokenType type;
  std::string text;
};

// Splits `sql` into tokens. Returns std::nullopt only for an unterminated
// string literal; unrecognized single characters become their own Symbol
// token so the parser can reject them explicitly instead of the lexer
// silently dropping input.
std::optional<std::vector<Token>> Tokenize(const std::string& sql) {
  std::vector<Token> tokens;
  std::size_t pos = 0;
  while (true) {
    while (pos < sql.size() && std::isspace(static_cast<unsigned char>(sql[pos]))) ++pos;
    if (pos >= sql.size()) {
      tokens.push_back({TokenType::End, ""});
      return tokens;
    }
    const char c = sql[pos];
    if (IsIdentStart(c)) {
      const auto start = pos;
      while (pos < sql.size() && IsIdentChar(sql[pos])) ++pos;
      tokens.push_back({TokenType::Identifier, sql.substr(start, pos - start)});
    } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      const auto start = pos;
      while (pos < sql.size() && std::isdigit(static_cast<unsigned char>(sql[pos])) != 0) ++pos;
      tokens.push_back({TokenType::Number, sql.substr(start, pos - start)});
    } else if (c == '\'') {
      ++pos;
      const auto start = pos;
      while (pos < sql.size() && sql[pos] != '\'') ++pos;
      if (pos >= sql.size()) return std::nullopt;  // unterminated string literal
      tokens.push_back({TokenType::String, sql.substr(start, pos - start)});
      ++pos;
    } else {
      tokens.push_back({TokenType::Symbol, std::string(1, c)});
      ++pos;
    }
  }
}

// Bounds-safe cursor over a token stream that always ends in an End token:
// Next() never advances past it, so Peek()/Next() can't run off the end
// even if a caller keeps calling after AtEnd().
class TokenStream {
 public:
  explicit TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  const Token& Peek() const { return tokens_[pos_]; }
  const Token& Next() {
    const Token& current = tokens_[pos_];
    if (current.type != TokenType::End) ++pos_;
    return current;
  }
  bool AtEnd() const { return Peek().type == TokenType::End; }

  bool PeekSymbol(char c) const { return Peek().type == TokenType::Symbol && Peek().text.size() == 1 && Peek().text[0] == c; }
  bool PeekKeyword(const std::string& lower) const {
    return Peek().type == TokenType::Identifier && ToLower(Peek().text) == lower;
  }

  bool ExpectKeyword(const std::string& lower, std::string& error) {
    if (!PeekKeyword(lower)) {
      error = "expected '" + lower + "'";
      return false;
    }
    Next();
    return true;
  }

  bool ExpectSymbol(char c, std::string& error) {
    if (!PeekSymbol(c)) {
      error = std::string("expected '") + c + "'";
      return false;
    }
    Next();
    return true;
  }

  std::optional<std::string> ExpectIdentifier(std::string& error) {
    if (Peek().type != TokenType::Identifier) {
      error = "expected an identifier";
      return std::nullopt;
    }
    return Next().text;
  }

  // A literal value: a numeric literal comes back with a value; a string
  // literal is syntactically accepted (valid SQL) but reported as
  // std::nullopt since AntSQL's distribution keys are integers.
  bool ExpectLiteral(std::optional<std::uint32_t>& out, std::string& error) {
    if (Peek().type == TokenType::Number) {
      const auto text = Next().text;
      char* end = nullptr;
      const auto value = std::strtoul(text.c_str(), &end, 10);
      if (end == text.c_str() || *end != '\0' || value > 0xFFFFFFFFULL) {
        error = "integer literal out of range: " + text;
        return false;
      }
      out = static_cast<std::uint32_t>(value);
      return true;
    }
    if (Peek().type == TokenType::String) {
      Next();
      out = std::nullopt;
      return true;
    }
    error = "expected a literal value";
    return false;
  }

 private:
  std::vector<Token> tokens_;
  std::size_t pos_{0};
};

const std::unordered_set<std::string>& DecomposableAggregates() {
  static const std::unordered_set<std::string> kAggregates = {"count", "sum", "min", "max"};
  return kAggregates;
}

// Consumes the WHERE clause, if present, common to SELECT/UPDATE/DELETE:
// `WHERE <col> = <int literal>`. Only a single predicate is supported; a
// trailing AND is reported as an explicit error rather than silently
// parsing just the first conjunct. Returns false only on a real syntax
// error (`error` is set); a well-formed predicate against a column other
// than `key_column` is not an error — it just leaves `partition` unset, so
// SqlValidator uniformly rejects any statement without a partition.
bool ParseOptionalWhere(TokenStream& tokens, const std::string& key_column,
                        std::optional<std::uint32_t>& partition, std::string& error) {
  if (!tokens.PeekKeyword("where")) return true;
  tokens.Next();
  const auto column = tokens.ExpectIdentifier(error);
  if (!column) return false;
  if (!tokens.ExpectSymbol('=', error)) return false;
  std::optional<std::uint32_t> value;
  if (!tokens.ExpectLiteral(value, error)) return false;
  if (ToLower(*column) == key_column) partition = value;
  if (tokens.PeekKeyword("and")) {
    error = "v1 supports at most one WHERE predicate";
    return false;
  }
  return true;
}

bool ConsumeStatementEnd(TokenStream& tokens, std::string& error) {
  if (tokens.PeekSymbol(';')) tokens.Next();
  if (!tokens.AtEnd()) {
    error = "unexpected trailing input";
    return false;
  }
  return true;
}

class StatementParser {
 public:
  explicit StatementParser(const std::unordered_map<std::string, std::string>& keys) : keys_(keys) {}

  std::variant<ParsedStatement, std::string> Parse(TokenStream& tokens) {
    if (tokens.PeekKeyword("select")) return ParseSelect(tokens);
    if (tokens.PeekKeyword("insert")) return ParseInsert(tokens);
    if (tokens.PeekKeyword("update")) return ParseUpdate(tokens);
    if (tokens.PeekKeyword("delete")) return ParseDelete(tokens);
    // Not a syntax error: a recognized-but-out-of-scope or unrelated
    // statement is simply Unsupported, matched by SqlValidator already.
    return ParsedStatement{};
  }

 private:
  std::optional<std::string> DistributionKey(const std::string& table, std::string& error) {
    const auto found = keys_.find(ToLower(table));
    if (found == keys_.end()) {
      error = "unknown table: " + table;
      return std::nullopt;
    }
    return found->second;
  }

  std::variant<ParsedStatement, std::string> ParseSelect(TokenStream& tokens) {
    tokens.Next();  // SELECT
    std::string error;
    bool is_aggregate = false;

    if (tokens.PeekSymbol('*')) {
      tokens.Next();
    } else {
      const auto first = tokens.ExpectIdentifier(error);
      if (!first) return error;
      if (DecomposableAggregates().contains(ToLower(*first)) && tokens.PeekSymbol('(')) {
        tokens.Next();
        if (tokens.PeekSymbol('*')) {
          tokens.Next();
        } else if (!tokens.ExpectIdentifier(error)) {
          return error;
        }
        if (!tokens.ExpectSymbol(')', error)) return error;
        is_aggregate = true;
      } else {
        while (tokens.PeekSymbol(',')) {
          tokens.Next();
          if (!tokens.ExpectIdentifier(error)) return error;
        }
      }
    }

    if (!tokens.ExpectKeyword("from", error)) return error;
    const auto table = tokens.ExpectIdentifier(error);
    if (!table) return error;
    const auto key_column = DistributionKey(*table, error);
    if (!key_column) return error;

    std::optional<std::uint32_t> partition;
    if (!ParseOptionalWhere(tokens, *key_column, partition, error)) return error;
    if (!ConsumeStatementEnd(tokens, error)) return error;
    return ParsedStatement{QueryKind::Select, *table, partition, is_aggregate};
  }

  std::variant<ParsedStatement, std::string> ParseInsert(TokenStream& tokens) {
    tokens.Next();  // INSERT
    std::string error;
    if (!tokens.ExpectKeyword("into", error)) return error;
    const auto table = tokens.ExpectIdentifier(error);
    if (!table) return error;
    const auto key_column = DistributionKey(*table, error);
    if (!key_column) return error;

    if (!tokens.ExpectSymbol('(', error)) return error;
    std::vector<std::string> columns;
    for (;;) {
      const auto column = tokens.ExpectIdentifier(error);
      if (!column) return error;
      columns.push_back(ToLower(*column));
      if (!tokens.PeekSymbol(',')) break;
      tokens.Next();
    }
    if (!tokens.ExpectSymbol(')', error)) return error;

    if (!tokens.ExpectKeyword("values", error)) return error;
    if (!tokens.ExpectSymbol('(', error)) return error;
    std::vector<std::optional<std::uint32_t>> values;
    for (;;) {
      std::optional<std::uint32_t> value;
      if (!tokens.ExpectLiteral(value, error)) return error;
      values.push_back(value);
      if (!tokens.PeekSymbol(',')) break;
      tokens.Next();
    }
    if (!tokens.ExpectSymbol(')', error)) return error;

    if (columns.size() != values.size()) return std::string("column/value count mismatch");
    std::optional<std::uint32_t> partition;
    for (std::size_t i = 0; i < columns.size(); ++i) {
      if (columns[i] == *key_column) {
        partition = values[i];
        break;
      }
    }
    if (!ConsumeStatementEnd(tokens, error)) return error;
    return ParsedStatement{QueryKind::Insert, *table, partition, false};
  }

  std::variant<ParsedStatement, std::string> ParseUpdate(TokenStream& tokens) {
    tokens.Next();  // UPDATE
    std::string error;
    const auto table = tokens.ExpectIdentifier(error);
    if (!table) return error;
    const auto key_column = DistributionKey(*table, error);
    if (!key_column) return error;

    if (!tokens.ExpectKeyword("set", error)) return error;
    for (;;) {
      if (!tokens.ExpectIdentifier(error)) return error;
      if (!tokens.ExpectSymbol('=', error)) return error;
      std::optional<std::uint32_t> value;
      if (!tokens.ExpectLiteral(value, error)) return error;
      if (!tokens.PeekSymbol(',')) break;
      tokens.Next();
    }

    std::optional<std::uint32_t> partition;
    if (!ParseOptionalWhere(tokens, *key_column, partition, error)) return error;
    if (!ConsumeStatementEnd(tokens, error)) return error;
    return ParsedStatement{QueryKind::Update, *table, partition, false};
  }

  std::variant<ParsedStatement, std::string> ParseDelete(TokenStream& tokens) {
    tokens.Next();  // DELETE
    std::string error;
    if (!tokens.ExpectKeyword("from", error)) return error;
    const auto table = tokens.ExpectIdentifier(error);
    if (!table) return error;
    const auto key_column = DistributionKey(*table, error);
    if (!key_column) return error;

    std::optional<std::uint32_t> partition;
    if (!ParseOptionalWhere(tokens, *key_column, partition, error)) return error;
    if (!ConsumeStatementEnd(tokens, error)) return error;
    return ParsedStatement{QueryKind::Delete, *table, partition, false};
  }

  const std::unordered_map<std::string, std::string>& keys_;
};

}  // namespace

SimpleSqlParser::SimpleSqlParser(std::unordered_map<std::string, std::string> distribution_key_by_table) {
  distribution_key_by_table_.reserve(distribution_key_by_table.size());
  for (auto& [table, column] : distribution_key_by_table) {
    distribution_key_by_table_.emplace(ToLower(table), ToLower(column));
  }
}

std::variant<ParsedStatement, std::string> SimpleSqlParser::Parse(const std::string& sql) const {
  const auto tokens = Tokenize(sql);
  if (!tokens) return std::string("unterminated string literal");
  TokenStream stream(*tokens);
  StatementParser parser(distribution_key_by_table_);
  return parser.Parse(stream);
}

}  // namespace antsql
