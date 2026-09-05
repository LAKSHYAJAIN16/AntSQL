#include "antsql/in_process_forwarder.hpp"

#include <utility>

namespace antsql {

void InProcessForwarder::Register(std::string node_id, Gateway& gateway) {
  std::scoped_lock lock(mutex_);
  gateways_.insert_or_assign(std::move(node_id), &gateway);
}

void InProcessForwarder::Unregister(const std::string& node_id) {
  std::scoped_lock lock(mutex_);
  gateways_.erase(node_id);
}

QueryResponse InProcessForwarder::Forward(const std::string& neighbor,
                                          const QueryRequest& request) {
  Gateway* gateway = nullptr;
  {
    std::scoped_lock lock(mutex_);
    const auto found = gateways_.find(neighbor);
    if (found == gateways_.end()) {
      return {QueryStatus::ShardUnavailable, "target gateway is unavailable: " + neighbor, 0.0};
    }
    gateway = found->second;
  }
  return gateway->Execute(request);
}

}  // namespace antsql
