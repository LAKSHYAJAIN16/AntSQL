#pragma once

#include "antsql/gateway.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace antsql {

// A process-local transport used by the native routing harness.  Production
// transports (gRPC) keep the same IForwarder contract, so routing behavior can
// be exercised without a network stack or PostgreSQL installation.
class InProcessForwarder final : public IForwarder {
 public:
  void Register(std::string node_id, Gateway& gateway);
  void Unregister(const std::string& node_id);
  QueryResponse Forward(const std::string& neighbor, const QueryRequest& request) override;

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, Gateway*> gateways_;
};

}  // namespace antsql
