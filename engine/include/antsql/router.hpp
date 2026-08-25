#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace antsql {

struct RouteKey {
  std::string table;
  std::uint32_t partition{};

  bool operator==(const RouteKey&) const = default;
};

struct RouteKeyHash {
  std::size_t operator()(const RouteKey& key) const noexcept;
};

struct Neighbor {
  std::string id;
  double link_latency_ms{};
};

struct RouteDecision {
  std::string next_hop;
  double score{};
  bool explored{};
};

struct RouterConfig {
  double evaporation_rate{0.02};
  double base_reinforcement{2.5};
  double reference_cost_ms{20.0};
  double minimum_reinforcement_fraction{0.02};
  double failure_penalty{0.25};
  double exploration_probability{0.05};
  double initial_pheromone{0.5};
  std::uint64_t random_seed{0};
};

class Router {
 public:
  explicit Router(RouterConfig config = {});

  std::optional<RouteDecision> Choose(
      const RouteKey& key,
      const std::vector<Neighbor>& candidates,
      const std::unordered_set<std::string>& visited = {});

  void ObserveSuccess(const RouteKey& key, const std::vector<std::string>& path,
                      double total_cost_ms);
  void ObserveFailure(const RouteKey& key, const std::vector<std::string>& path);
  void Evaporate();
  double Pheromone(const RouteKey& key, const std::string& neighbor) const;

 private:
  using NeighborScores = std::unordered_map<std::string, double>;
  RouterConfig config_;
  std::mt19937_64 rng_;
  std::unordered_map<RouteKey, NeighborScores, RouteKeyHash> pheromone_;

  double Score(const RouteKey& key, const Neighbor& candidate) const;
};

}  // namespace antsql
