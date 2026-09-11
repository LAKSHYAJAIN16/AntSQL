#include "antsql/wire.hpp"

#include <cassert>

namespace {
using namespace antsql;

void WireRequestRoundTripsTests() {
  QueryRequest request{"SELECT * FROM readings WHERE site_id = 7",
                       {QueryKind::Select, "readings", 7, false},
                       {"source", "relay"},
                       14};
  const auto encoded = wire::EncodeRequest(request);
  const auto decoded = wire::DecodeRequest(encoded);
  assert(decoded.has_value());
  assert(decoded->sql == request.sql);
  assert(decoded->statement.kind == QueryKind::Select);
  assert(decoded->statement.table == "readings");
  assert(decoded->statement.partition == 7);
  assert(decoded->statement.is_decomposable_aggregate == false);
  assert(decoded->route == request.route);
  assert(decoded->hop_budget == 14);
}

void WireRequestWithoutPartitionRoundTripsTests() {
  QueryRequest request{"SELECT count(*) FROM readings", {QueryKind::Select, "readings", std::nullopt, true},
                       {}, 16};
  const auto decoded = wire::DecodeRequest(wire::EncodeRequest(request));
  assert(decoded.has_value());
  assert(!decoded->statement.partition.has_value());
  assert(decoded->statement.is_decomposable_aggregate == true);
  assert(decoded->route.empty());
}

void WireResponseRoundTripsTests() {
  QueryResponse response{QueryStatus::Ok, "executed on owner", 12.5};
  const auto decoded = wire::DecodeResponse(wire::EncodeResponse(response));
  assert(decoded.has_value());
  assert(decoded->status == QueryStatus::Ok);
  assert(decoded->payload == "executed on owner");
  assert(decoded->elapsed_ms == 12.5);
}

void WireDecodeRejectsTruncatedAndMalformedBytesTests() {
  const auto encoded = wire::EncodeRequest(
      {"SELECT 1", {QueryKind::Select, "t", 0, false}, {}, 1});
  assert(!wire::DecodeRequest(encoded.substr(0, encoded.size() - 1)).has_value());
  assert(!wire::DecodeRequest(encoded + std::string("trailing")).has_value());
  assert(!wire::DecodeRequest("").has_value());

  // Empty sql (4-byte zero length) puts the kind byte at offset 4.
  std::string request_with_bad_kind = wire::EncodeRequest({"", {QueryKind::Select, "", 0, false}, {}, 0});
  request_with_bad_kind[4] = static_cast<char>(200);
  assert(!wire::DecodeRequest(request_with_bad_kind).has_value());

  std::string bad_status = wire::EncodeResponse({QueryStatus::Ok, "", 0.0});
  bad_status[0] = static_cast<char>(200);
  assert(!wire::DecodeResponse(bad_status).has_value());
}

struct WireTestRegistration {
  WireTestRegistration() {
    WireRequestRoundTripsTests();
    WireRequestWithoutPartitionRoundTripsTests();
    WireResponseRoundTripsTests();
    WireDecodeRejectsTruncatedAndMalformedBytesTests();
  }
} wire_test_registration;
}  // namespace
