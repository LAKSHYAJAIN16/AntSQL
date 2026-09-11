#include "antsql/wire.hpp"

#include <cstdint>
#include <cstring>

namespace antsql::wire {
namespace {

constexpr auto kMaxQueryKind = static_cast<std::uint8_t>(QueryKind::Unsupported);
constexpr auto kMaxQueryStatus = static_cast<std::uint8_t>(QueryStatus::ExecutionError);

void WriteUint32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

void WriteString(std::string& out, const std::string& value) {
  WriteUint32(out, static_cast<std::uint32_t>(value.size()));
  out.append(value);
}

void WriteDouble(std::string& out, double value) {
  // Both ends of this wire format run on the little-endian architectures
  // AntSQL currently targets (x86-64/ARM64); this is a raw byte copy, not a
  // portable IEEE-754 byte-order encoding.
  char bytes[sizeof(double)];
  std::memcpy(bytes, &value, sizeof(double));
  out.append(bytes, sizeof(double));
}

// Bounds-checked cursor over an already-fully-received buffer. Every read
// can fail (truncated/malformed input from a peer) without ever reading or
// writing outside `data`.
class Reader {
 public:
  explicit Reader(const std::string& data) : data_(data) {}

  bool ReadUint32(std::uint32_t& out) {
    if (pos_ + 4 > data_.size()) return false;
    out = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_])) << 24) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_ + 1])) << 16) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_ + 2])) << 8) |
          static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_ + 3]));
    pos_ += 4;
    return true;
  }

  bool ReadString(std::string& out) {
    std::uint32_t length = 0;
    if (!ReadUint32(length)) return false;
    if (pos_ + length > data_.size()) return false;
    out.assign(data_, pos_, length);
    pos_ += length;
    return true;
  }

  bool ReadByte(std::uint8_t& out) {
    if (pos_ + 1 > data_.size()) return false;
    out = static_cast<std::uint8_t>(data_[pos_]);
    pos_ += 1;
    return true;
  }

  bool ReadDouble(double& out) {
    if (pos_ + sizeof(double) > data_.size()) return false;
    std::memcpy(&out, data_.data() + pos_, sizeof(double));
    pos_ += sizeof(double);
    return true;
  }

  bool AtEnd() const { return pos_ == data_.size(); }

 private:
  const std::string& data_;
  std::size_t pos_{0};
};

}  // namespace

std::string EncodeRequest(const QueryRequest& request) {
  std::string out;
  WriteString(out, request.sql);
  out.push_back(static_cast<char>(request.statement.kind));
  WriteString(out, request.statement.table);
  const bool has_partition = request.statement.partition.has_value();
  out.push_back(has_partition ? 1 : 0);
  if (has_partition) WriteUint32(out, *request.statement.partition);
  out.push_back(request.statement.is_decomposable_aggregate ? 1 : 0);
  WriteUint32(out, static_cast<std::uint32_t>(request.route.size()));
  for (const auto& hop : request.route) WriteString(out, hop);
  WriteUint32(out, request.hop_budget);
  return out;
}

std::string EncodeResponse(const QueryResponse& response) {
  std::string out;
  out.push_back(static_cast<char>(response.status));
  WriteString(out, response.payload);
  WriteDouble(out, response.elapsed_ms);
  return out;
}

std::optional<QueryRequest> DecodeRequest(const std::string& bytes) {
  Reader reader(bytes);
  QueryRequest request;
  std::uint8_t kind = 0;
  std::uint8_t has_partition = 0;
  std::uint8_t is_aggregate = 0;
  std::uint32_t route_size = 0;

  if (!reader.ReadString(request.sql)) return std::nullopt;
  if (!reader.ReadByte(kind) || kind > kMaxQueryKind) return std::nullopt;
  request.statement.kind = static_cast<QueryKind>(kind);
  if (!reader.ReadString(request.statement.table)) return std::nullopt;
  if (!reader.ReadByte(has_partition)) return std::nullopt;
  if (has_partition) {
    std::uint32_t partition = 0;
    if (!reader.ReadUint32(partition)) return std::nullopt;
    request.statement.partition = partition;
  }
  if (!reader.ReadByte(is_aggregate)) return std::nullopt;
  request.statement.is_decomposable_aggregate = is_aggregate != 0;

  if (!reader.ReadUint32(route_size)) return std::nullopt;
  // Deliberately not reserve()'d: route_size is attacker/peer controlled and
  // each remaining ReadString call is itself bounds-checked against the
  // actual buffer, so an oversized count fails on the first short read
  // instead of forcing a large speculative allocation up front.
  for (std::uint32_t i = 0; i < route_size; ++i) {
    std::string hop;
    if (!reader.ReadString(hop)) return std::nullopt;
    request.route.push_back(std::move(hop));
  }
  if (!reader.ReadUint32(request.hop_budget)) return std::nullopt;
  if (!reader.AtEnd()) return std::nullopt;
  return request;
}

std::optional<QueryResponse> DecodeResponse(const std::string& bytes) {
  Reader reader(bytes);
  QueryResponse response;
  std::uint8_t status = 0;
  if (!reader.ReadByte(status) || status > kMaxQueryStatus) return std::nullopt;
  response.status = static_cast<QueryStatus>(status);
  if (!reader.ReadString(response.payload)) return std::nullopt;
  if (!reader.ReadDouble(response.elapsed_ms)) return std::nullopt;
  if (!reader.AtEnd()) return std::nullopt;
  return response;
}

}  // namespace antsql::wire
