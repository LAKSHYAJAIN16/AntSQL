// AntSQL Colony Demo — the application that shows the engine off.
//
// Boots a real 5-node ring-topology AntSQL cluster: each node is its own
// Router, its own TableStoreExecutor (real, queryable storage — not a mock),
// and its own TcpServer listening on a real loopback socket, wired to its
// two ring neighbors over real TcpForwarder/TCP connections and AntSQL's
// hand-rolled wire format. No node has a map of the whole ring; each only
// knows its two neighbors and follows pheromone left by past queries.
//
// The story told in three phases:
//   1. Cold start: every query from node 0 to the account living on node 3
//      can go clockwise or counter-clockwise around the ring — both cost
//      the same, nothing tells the colony which to prefer. Watch positive
//      feedback break the symmetry and every gateway commit to one route.
//   2. A trail map: pheromone strength on every ring edge for that route.
//   3. Failure: kill the node on the colony's current favorite path mid-run.
//      Throughput dips, then recovers as pheromone drains off the dead edge
//      and builds on the surviving path — with no coordinator ever
//      recomputing a global plan. A per-tick log is written to
//      results/colony_demo_run.csv, the same convention the paper's other
//      resilience studies use.
#include "antsql/gateway.hpp"
#include "antsql/in_memory_topology.hpp"
#include "antsql/router.hpp"
#include "antsql/simple_sql_parser.hpp"
#include "antsql/sql.hpp"
#include "antsql/table_store_executor.hpp"
#include "antsql/tcp_forwarder.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace antsql;

namespace {

constexpr int kRingSize = 5;
constexpr int kOwnerNode = 3;       // node that owns partition 3
constexpr double kLinkLatencyMs = 1.0;

std::string NodeId(int i) { return "shard-" + std::to_string((i % kRingSize + kRingSize) % kRingSize); }

struct Node {
  std::string id;
  Router router{RouterConfig{.evaporation_rate = 0.04, .exploration_probability = 0.08,
                              .random_seed = static_cast<std::uint64_t>(1000)}};
  InMemoryTopology topology;
  TableStoreExecutor storage;  // real embedded storage, memory-backed for this demo
  TcpForwarder forwarder;
  std::unique_ptr<Gateway> gateway;
  std::unique_ptr<TcpServer> server;
};

QueryRequest MakeRequest(QueryKind kind, const std::string& sql, std::uint32_t partition) {
  return QueryRequest{sql, ParsedStatement{kind, "accounts", partition, false}, {}, 32};
}

void PrintTrailMap(std::vector<std::unique_ptr<Node>>& nodes, const RouteKey& key) {
  std::printf("\n  pheromone trail for accounts[id=%u] (ring edges, both directions):\n", key.partition);
  for (int i = 0; i < kRingSize; ++i) {
    const auto forward = NodeId(i + 1);
    const auto backward = NodeId(i - 1);
    std::printf("    %s -> %s : %6.2f      %s -> %s : %6.2f\n", NodeId(i).c_str(), forward.c_str(),
                nodes[i]->router.Pheromone(key, forward), NodeId(i).c_str(), backward.c_str(),
                nodes[i]->router.Pheromone(key, backward));
  }
}

}  // namespace

int main() {
  std::printf("AntSQL Colony Demo — a %d-node ring, no node with a map of the whole thing.\n", kRingSize);

  std::vector<std::unique_ptr<Node>> nodes;
  for (int i = 0; i < kRingSize; ++i) {
    auto node = std::make_unique<Node>();
    node->id = NodeId(i);
    node->topology.SetNeighbors({{NodeId(i + 1), kLinkLatencyMs}, {NodeId(i - 1), kLinkLatencyMs}});
    if (i == kOwnerNode) node->topology.SetOwnedRoutes({{"accounts", kOwnerNode}});
    nodes.push_back(std::move(node));
  }
  for (int i = 0; i < kRingSize; ++i) {
    auto& node = *nodes[i];
    node.gateway = std::make_unique<Gateway>(node.id, node.router, node.topology, node.storage, node.forwarder);
    node.server = std::make_unique<TcpServer>(
        "127.0.0.1", 0, [&node](const QueryRequest& request) { return node.gateway->Execute(request); });
    node.server->Start();
  }
  for (int i = 0; i < kRingSize; ++i) {
    auto& node = *nodes[i];
    node.forwarder.AddPeer(NodeId(i + 1), "127.0.0.1", nodes[(i + 1) % kRingSize]->server->BoundPort());
    node.forwarder.AddPeer(NodeId(i - 1), "127.0.0.1", nodes[(i - 1 + kRingSize) % kRingSize]->server->BoundPort());
  }
  std::printf("Cluster up. accounts[id=%d] is owned by %s.\n", kOwnerNode, NodeId(kOwnerNode).c_str());

  auto& entry = *nodes[0];
  const RouteKey key{"accounts", kOwnerNode};

  {
    const auto response = entry.gateway->Execute(
        MakeRequest(QueryKind::Insert, "INSERT INTO accounts (id, owner, balance) VALUES (3, 'ada', 1000)",
                    kOwnerNode));
    std::printf("\nSeed insert via %s -> %s (elapsed %.3fms)\n", entry.id.c_str(), response.payload.c_str(),
                response.elapsed_ms);
  }

  std::ofstream csv("results/colony_demo_run.csv");
  csv << "phase,tick,status,elapsed_ms\n";

  auto run_tick = [&](int phase, int tick) {
    auto response = entry.gateway->Execute(
        MakeRequest(QueryKind::Select, "SELECT * FROM accounts WHERE id = 3", kOwnerNode));
    csv << phase << ',' << tick << ',' << (response.status == QueryStatus::Ok ? "ok" : "fail") << ','
        << response.elapsed_ms << '\n';
    return response;
  };

  std::printf("\nPhase 1 — cold start: 40 reads of accounts[id=3] from %s, no global topology anywhere.\n",
              entry.id.c_str());
  int successes = 0;
  for (int tick = 0; tick < 40; ++tick) {
    if (run_tick(1, tick).status == QueryStatus::Ok) ++successes;
    entry.router.Evaporate();
  }
  std::printf("  %d/40 reads succeeded.\n", successes);
  PrintTrailMap(nodes, key);

  // Both ring directions have identical configured link latency, so nothing
  // here tells the colony which way is "correct" — this is a symmetry-break:
  // whichever direction got reinforced first by chance wins by autocatalysis
  // (the same take-off dynamic as the classic ant double-bridge experiment),
  // and every gateway commits to it independently with no coordinator ever
  // comparing the two options against each other.
  const bool favors_counter_clockwise =
      entry.router.Pheromone(key, NodeId(-1)) > entry.router.Pheromone(key, NodeId(1));
  std::printf("\n  the colony committed to the %s route (via %s) — pure positive feedback,\n"
              "  since both ring directions cost the same and nothing told it which to prefer.\n",
              favors_counter_clockwise ? "counter-clockwise" : "clockwise",
              favors_counter_clockwise ? NodeId(-1).c_str() : NodeId(1).c_str());

  const int failure_node = favors_counter_clockwise ? ((0 - 1 + kRingSize) % kRingSize) : 1;
  std::printf("\nPhase 2 — kill %s, the node on the colony's current favorite path.\n",
              NodeId(failure_node).c_str());
  nodes[failure_node]->server->Stop();

  successes = 0;
  int recovered_at = -1;
  for (int tick = 0; tick < 80; ++tick) {
    const auto response = run_tick(2, tick);
    if (response.status == QueryStatus::Ok) {
      ++successes;
      if (recovered_at < 0) recovered_at = tick;
    }
    entry.router.Evaporate();
  }
  std::printf("  %d/80 reads succeeded after the failure (first post-kill success at tick %d).\n"
              "  See results/colony_demo_run.csv for the full per-tick trace.\n",
              successes, recovered_at);
  PrintTrailMap(nodes, key);
  std::printf("\n  pheromone on the dead edge has drained toward the failure penalty floor; the\n"
              "  surviving path now carries the trail — no coordinator ever recomputed a plan.\n");

  {
    // Prove this was real storage, not just routing plumbing: read the row
    // straight off shard-3's own executor, bypassing the ring entirely.
    const auto direct = nodes[kOwnerNode]->storage.DumpRow("accounts", kOwnerNode);
    std::printf("\n  the row physically lives on %s: %s\n", NodeId(kOwnerNode).c_str(), direct.c_str());
  }

  for (auto& node : nodes) {
    if (node->server) node->server->Stop();
  }
  std::printf("\nDemo complete.\n");
  return 0;
}
