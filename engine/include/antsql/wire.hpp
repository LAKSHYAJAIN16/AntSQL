#pragma once

#include "antsql/gateway.hpp"

#include <optional>
#include <string>

namespace antsql::wire {

// A minimal, hand-rolled binary wire format for QueryRequest/QueryResponse.
// It exists so the native TCP transport (engine/include/antsql/tcp_forwarder.hpp)
// can move real bytes between gateway processes without a protobuf/gRPC
// toolchain. Encode/decode are pure and socket-free so they can be unit
// tested and reused if a future transport frames messages differently.
std::string EncodeRequest(const QueryRequest& request);
std::string EncodeResponse(const QueryResponse& response);

// Returns std::nullopt if bytes is truncated, malformed, has an
// out-of-range enum value, or has trailing data beyond one encoded message.
std::optional<QueryRequest> DecodeRequest(const std::string& bytes);
std::optional<QueryResponse> DecodeResponse(const std::string& bytes);

}  // namespace antsql::wire
