#include "antsql/in_memory_topology.hpp"

#include <utility>

namespace antsql {

bool InMemoryTopology::Owns(const RouteKey& key) const {
  std::scoped_lock lock(mutex_);
  return owned_routes_.contains(key);
}

std::vector<Neighbor> InMemoryTopology::Neighbors() const {
  std::scoped_lock lock(mutex_);
  return neighbors_;
}

void InMemoryTopology::SetOwnedRoutes(std::unordered_set<RouteKey, RouteKeyHash> routes) {
  std::scoped_lock lock(mutex_);
  owned_routes_ = std::move(routes);
}

void InMemoryTopology::SetNeighbors(std::vector<Neighbor> neighbors) {
  std::scoped_lock lock(mutex_);
  neighbors_ = std::move(neighbors);
}

}  // namespace antsql
