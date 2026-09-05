#pragma once

#include "antsql/gateway.hpp"

#include <mutex>
#include <unordered_set>
#include <vector>

namespace antsql {

// Mutable topology state for local integration tests and the future native
// failure harness. Production discovery can provide ITopology independently.
class InMemoryTopology final : public ITopology {
 public:
  bool Owns(const RouteKey& key) const override;
  std::vector<Neighbor> Neighbors() const override;

  void SetOwnedRoutes(std::unordered_set<RouteKey, RouteKeyHash> routes);
  void SetNeighbors(std::vector<Neighbor> neighbors);

 private:
  mutable std::mutex mutex_;
  std::unordered_set<RouteKey, RouteKeyHash> owned_routes_;
  std::vector<Neighbor> neighbors_;
};

}  // namespace antsql
