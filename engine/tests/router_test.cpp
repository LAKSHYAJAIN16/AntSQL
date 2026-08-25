#include "antsql/router.hpp"

#include <cassert>
#include <iostream>

using antsql::Neighbor;
using antsql::RouteKey;
using antsql::Router;
using antsql::RouterConfig;

int main() {
  const RouteKey key{"readings", 7};
  Router router(RouterConfig{.exploration_probability = 0.0, .random_seed = 42});
  const std::vector<Neighbor> neighbors{{"fast", 1.0}, {"slow", 2.0}};

  auto initial = router.Choose(key, neighbors);
  assert(initial && initial->next_hop == "fast");

  router.ObserveSuccess(key, {"source", "slow", "owner"}, 10.0);
  assert(router.Pheromone(key, "slow") > router.Pheromone(key, "fast"));
  auto learned = router.Choose(key, neighbors);
  assert(learned && learned->next_hop == "slow");

  const std::unordered_set<std::string> visited{"slow"};
  auto no_cycle = router.Choose(key, neighbors, visited);
  assert(no_cycle && no_cycle->next_hop == "fast");

  const auto before_failure = router.Pheromone(key, "slow");
  router.ObserveFailure(key, {"source", "slow"});
  assert(router.Pheromone(key, "slow") < before_failure);

  const auto before_evaporation = router.Pheromone(key, "slow");
  router.Evaporate();
  assert(router.Pheromone(key, "slow") < before_evaporation);
  std::cout << "router tests passed\n";
}
