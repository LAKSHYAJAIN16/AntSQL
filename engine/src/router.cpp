#include "antsql/router.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace antsql {

std::size_t RouteKeyHash::operator()(const RouteKey& key) const noexcept {
  return std::hash<std::string>{}(key.table) ^ (std::hash<std::uint32_t>{}(key.partition) << 1U);
}

Router::Router(RouterConfig config) : config_(config), rng_(config.random_seed) {}

double Router::Pheromone(const RouteKey& key, const std::string& neighbor) const {
  const auto route = pheromone_.find(key);
  if (route == pheromone_.end()) return config_.initial_pheromone;
  const auto score = route->second.find(neighbor);
  return score == route->second.end() ? config_.initial_pheromone : score->second;
}

double Router::Score(const RouteKey& key, const Neighbor& candidate) const {
  return Pheromone(key, candidate.id) / std::max(candidate.link_latency_ms, 0.001);
}

std::optional<RouteDecision> Router::Choose(
    const RouteKey& key, const std::vector<Neighbor>& candidates,
    const std::unordered_set<std::string>& visited) {
  std::vector<Neighbor> eligible;
  std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(eligible),
               [&visited](const Neighbor& neighbor) { return !visited.contains(neighbor.id); });
  if (eligible.empty()) return std::nullopt;

  std::bernoulli_distribution explore(config_.exploration_probability);
  if (explore(rng_)) {
    std::uniform_int_distribution<std::size_t> pick(0, eligible.size() - 1);
    const auto& chosen = eligible[pick(rng_)];
    return RouteDecision{chosen.id, Score(key, chosen), true};
  }

  const auto best = std::max_element(eligible.begin(), eligible.end(),
      [this, &key](const Neighbor& lhs, const Neighbor& rhs) { return Score(key, lhs) < Score(key, rhs); });
  return RouteDecision{best->id, Score(key, *best), false};
}

void Router::ObserveSuccess(const RouteKey& key, const std::vector<std::string>& path,
                            double total_cost_ms) {
  if (path.size() < 2) return;
  const auto quality = config_.reference_cost_ms / std::max(total_cost_ms, 0.001);
  const auto reinforcement = config_.base_reinforcement * std::clamp(
      quality, config_.minimum_reinforcement_fraction, 1.0);
  auto& scores = pheromone_[key];
  for (std::size_t index = 0; index + 1 < path.size(); ++index) {
    scores[path[index + 1]] += reinforcement;
  }
}

void Router::ObserveFailure(const RouteKey& key, const std::vector<std::string>& path) {
  if (path.size() < 2) return;
  auto& scores = pheromone_[key];
  const auto& failed_next_hop = path.back();
  scores[failed_next_hop] = std::max(0.0, Pheromone(key, failed_next_hop) * config_.failure_penalty);
}

void Router::Evaporate() {
  for (auto& [_, scores] : pheromone_) {
    for (auto& [__, amount] : scores) amount *= (1.0 - config_.evaporation_rate);
  }
}

}  // namespace antsql
