#include "antsql/table_store_executor.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
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

// A trimmed copy of SimpleSqlParser's tokenizer: this executor and the
// router's SimpleSqlParser answer different questions (can this be routed
// safely vs. what row does this describe) and deliberately don't share a
// grammar module, so each can change independently.
std::vector<Token> Tokenize(const std::string& sql) {
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
      tokens.push_back({TokenType::String, sql.substr(start, pos - start)});
      if (pos < sql.size()) ++pos;
    } else {
      tokens.push_back({TokenType::Symbol, std::string(1, c)});
      ++pos;
    }
  }
}

class TokenStream {
 public:
  explicit TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
  const Token& Peek() const { return tokens_[pos_]; }
  const Token& Next() {
    const Token& current = tokens_[pos_];
    if (current.type != TokenType::End) ++pos_;
    return current;
  }
  bool PeekSymbol(char c) const {
    return Peek().type == TokenType::Symbol && Peek().text.size() == 1 && Peek().text[0] == c;
  }
  bool PeekKeyword(const std::string& lower) const {
    return Peek().type == TokenType::Identifier && ToLower(Peek().text) == lower;
  }
  bool SkipToKeyword(const std::string& lower) {
    while (Peek().type != TokenType::End) {
      if (PeekKeyword(lower)) return true;
      Next();
    }
    return false;
  }

 private:
  std::vector<Token> tokens_;
  std::size_t pos_{0};
};

std::string LiteralText(const Token& token) {
  return token.type == TokenType::String ? token.text : token.text;
}

// Parses `(a, b, c)` (identifiers) starting at the current '('.
std::vector<std::string> ParseIdentList(TokenStream& tokens) {
  std::vector<std::string> out;
  if (!tokens.PeekSymbol('(')) return out;
  tokens.Next();
  while (tokens.Peek().type == TokenType::Identifier || tokens.Peek().type == TokenType::Symbol) {
    if (tokens.PeekSymbol(')')) break;
    if (tokens.Peek().type == TokenType::Identifier) out.push_back(ToLower(tokens.Next().text));
    if (tokens.PeekSymbol(',')) tokens.Next();
  }
  if (tokens.PeekSymbol(')')) tokens.Next();
  return out;
}

// Parses `(1, 'x', 2)` (literals) starting at the current '('.
std::vector<std::string> ParseLiteralList(TokenStream& tokens) {
  std::vector<std::string> out;
  if (!tokens.PeekSymbol('(')) return out;
  tokens.Next();
  while (!tokens.PeekSymbol(')') && tokens.Peek().type != TokenType::End) {
    const auto& tok = tokens.Peek();
    if (tok.type == TokenType::Number || tok.type == TokenType::String) {
      out.push_back(LiteralText(tokens.Next()));
    } else {
      tokens.Next();
    }
    if (tokens.PeekSymbol(',')) tokens.Next();
  }
  if (tokens.PeekSymbol(')')) tokens.Next();
  return out;
}

std::string FormatRow(const std::unordered_map<std::string, std::string>& row) {
  std::vector<std::string> keys;
  keys.reserve(row.size());
  for (const auto& [k, _] : row) keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  std::ostringstream out;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (i) out << ',';
    out << keys[i] << '=' << row.at(keys[i]);
  }
  return out.str();
}

}  // namespace

TableStoreExecutor::TableStoreExecutor(std::string wal_path) : wal_path_(std::move(wal_path)) {
  ReplayWal();
}

// Each WAL line carries the statement's routing-relevant facts (kind, table,
// partition) ahead of the original SQL text, tab-separated: replay must
// reconstruct the exact key a mutation was applied under, and that key isn't
// always recoverable by re-tokenizing the SQL alone (the distribution-key
// column name lives in SimpleSqlParser's table config, not in this
// executor), so it's cheaper and more robust to just log what Gateway
// already resolved.
void TableStoreExecutor::ReplayWal() {
  if (wal_path_.empty()) return;
  std::ifstream in(wal_path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    int kind_value{};
    std::string table;
    std::uint32_t partition{};
    iss >> kind_value >> table >> partition;
    iss.get();  // the tab separator
    std::string sql;
    std::getline(iss, sql);

    QueryRequest request;
    request.sql = sql;
    request.statement.kind = static_cast<QueryKind>(kind_value);
    request.statement.table = table;
    request.statement.partition = partition;
    ExecuteLocked(request);
  }
}

void TableStoreExecutor::AppendToWal(const QueryRequest& request) {
  if (wal_path_.empty()) return;
  std::ofstream out(wal_path_, std::ios::app);
  if (!out) return;
  std::string sanitized = request.sql;
  std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
  out << static_cast<int>(request.statement.kind) << ' ' << ToLower(request.statement.table) << ' '
      << *request.statement.partition << '\t' << sanitized << '\n';
}

QueryResponse TableStoreExecutor::Execute(const QueryRequest& request) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto response = ExecuteLocked(request);
  if (response.status == QueryStatus::Ok &&
      (request.statement.kind == QueryKind::Insert || request.statement.kind == QueryKind::Update ||
       request.statement.kind == QueryKind::Delete)) {
    AppendToWal(request);
  }
  return response;
}

QueryResponse TableStoreExecutor::ExecuteLocked(const QueryRequest& request) {
  const auto& stmt = request.statement;
  const std::uint32_t key = *stmt.partition;
  auto& table = tables_[ToLower(stmt.table)];

  switch (stmt.kind) {
    case QueryKind::Insert: {
      TokenStream fresh(Tokenize(request.sql));
      fresh.SkipToKeyword("into");
      fresh.Next();  // "into"
      fresh.Next();  // table identifier
      const auto columns = ParseIdentList(fresh);
      fresh.SkipToKeyword("values");
      fresh.Next();
      const auto values = ParseLiteralList(fresh);
      Row row;
      for (std::size_t i = 0; i < columns.size() && i < values.size(); ++i) row[columns[i]] = values[i];
      table[key] = std::move(row);
      return {QueryStatus::Ok, "INSERT 1", 0.0};
    }
    case QueryKind::Select: {
      const auto found = table.find(key);
      if (found == table.end()) return {QueryStatus::Ok, "", 0.0};
      if (stmt.is_decomposable_aggregate) return {QueryStatus::Ok, "1", 0.0};
      return {QueryStatus::Ok, FormatRow(found->second), 0.0};
    }
    case QueryKind::Update: {
      const auto found = table.find(key);
      if (found == table.end()) return {QueryStatus::ExecutionError, "no such row", 0.0};
      TokenStream fresh(Tokenize(request.sql));
      fresh.SkipToKeyword("set");
      fresh.Next();
      while (fresh.Peek().type == TokenType::Identifier) {
        const auto column = ToLower(fresh.Next().text);
        if (!fresh.PeekSymbol('=')) break;
        fresh.Next();
        const auto value_tok = fresh.Peek();
        if (value_tok.type == TokenType::Number || value_tok.type == TokenType::String) {
          found->second[column] = LiteralText(fresh.Next());
        }
        if (fresh.PeekSymbol(',')) fresh.Next(); else break;
      }
      return {QueryStatus::Ok, "UPDATE 1", 0.0};
    }
    case QueryKind::Delete: {
      const auto erased = table.erase(key);
      return {QueryStatus::Ok, erased ? "DELETE 1" : "DELETE 0", 0.0};
    }
    default:
      return {QueryStatus::UnsupportedSql, "executor cannot run this statement kind", 0.0};
  }
}

std::size_t TableStoreExecutor::RowCount(const std::string& table) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tables_.find(ToLower(table));
  return found == tables_.end() ? 0 : found->second.size();
}

std::string TableStoreExecutor::DumpRow(const std::string& table, std::uint32_t key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tables_.find(ToLower(table));
  if (found == tables_.end()) return "";
  const auto row = found->second.find(key);
  return row == found->second.end() ? "" : FormatRow(row->second);
}

}  // namespace antsql
