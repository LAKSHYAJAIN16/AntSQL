#include "antsql/router.hpp"

#include <cassert>
#include <iostream>
#include <thread>

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

  // Concurrent stress test: TcpServer now serves connections one thread
  // per connection, so a Gateway's shared Router can be mutated from
  // multiple threads at once. Before mutex_ was added, this workload would
  // corrupt pheromone_/rng_ under a thread sanitizer (and could crash or
  // hang outright); the assertion here is that it completes cleanly.
  {
    Router shared_router(RouterConfig{.exploration_probability = 0.1, .random_seed = 7});
    const RouteKey shared_key{"orders", 3};
    const std::vector<Neighbor> shared_neighbors{{"a", 1.0}, {"b", 2.0}, {"c", 3.0}};
    constexpr int kThreads = 8;
    constexpr int kIterationsPerThread = 500;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&shared_router, &shared_key, &shared_neighbors, t] {
        for (int i = 0; i < kIterationsPerThread; ++i) {
          const auto decision = shared_router.Choose(shared_key, shared_neighbors);
          if (!decision) continue;
          if ((i + t) % 3 == 0) {
            shared_router.ObserveFailure(shared_key, {"source", decision->next_hop});
          } else {
            shared_router.ObserveSuccess(shared_key, {"source", decision->next_hop}, 5.0 + i);
          }
          if (i % 50 == 0) shared_router.Evaporate();
        }
      });
    }
    for (auto& worker : workers) worker.join();

    assert(shared_router.Pheromone(shared_key, "a") >= 0.0);
  }

  std::cout << "router tests passed\n";
}
