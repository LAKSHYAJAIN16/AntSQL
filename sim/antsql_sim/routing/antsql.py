"""The treatment: gossip-bounded stigmergic routing (related_work.md §5,
idea 9.4) with per-shard heterogeneous evaporation (idea 9.5).

Mechanism, deliberately close to AntNet's (JAIR98 §4, verified in
related_work.md §2a) with two departures:
  1. hop_count > 0 lets pheromone updates propagate beyond one hop via
     bounded-flood gossip, trading a little locality for faster
     convergence — hop_count=0 recovers pure AntNet-style locality exactly,
     which is what makes it a legitimate sweep parameter rather than a
     different algorithm.
  2. evaporation rate is per-shard (rho_hot vs rho_cold via an externally
     updated hot_shards set), not a single global constant.

v1 simplification, noted honestly: only forward/backward ant probes
reinforce pheromone; live query traffic is read-only. AntNet itself lets
data-packet traffic contribute too (JAIR98 §8.1) — a natural refinement,
left for later rather than risking a v1 bug in the reinforcement math.

Reinforcement is cost-sensitive (§ smoke-test finding, 2026-08-24): an
earlier flat "+= base_r on any success" version converged to *any*
reachable path, not a good one — a long wandering detour that eventually
stumbled onto the target got reinforced exactly as much as the shortest
path, which measurably broke steady-state cost even at zero churn. The
fix mirrors AntNet's own squashing-function idea (JAIR98 §4): reinforcement
scales down as path cost grows relative to reference_cost, with a floor so
a working-but-mediocre path still gets some credit (keeps a fallback route
alive under churn) rather than being driven to zero and forgotten.
"""
from __future__ import annotations

import networkx as nx
import numpy as np

from antsql_sim.routing.base import RoutingStrategy
from antsql_sim.topology import alive_neighbors, is_alive


class AntSQL(RoutingStrategy):
    name = "antsql"

    def __init__(
        self,
        graph,
        shard_map,
        node_ids,
        seed,
        probe_frac: float = 0.03,
        max_hops: int = 12,
        base_r: float = 2.5,
        reference_cost: float = 12.0,
        min_reinforcement_frac: float = 0.1,
        init_pher: float = 0.5,
        rho_hot: float = 0.02,
        rho_cold: float = 0.005,
        gossip_interval: int = 20,
        # Local pheromone is the safe serving default. Gossip is an explicit
        # experimental mode because its benefit depends on churn and warm-up.
        hop_count: int = 0,
        gossip_r: float = 0.03,
        gossip_discount: float = 0.5,
        gossip_shards_per_node: int = 2,
        exploit_probability: float = 0.95,
        latency_exponent: float = 1.0,
    ):
        super().__init__(graph, shard_map, node_ids, seed)
        self.probe_frac = probe_frac
        self.max_hops = max_hops
        self.base_r = base_r
        self.reference_cost = reference_cost
        self.min_reinforcement_frac = min_reinforcement_frac
        self._init_pher = init_pher
        self.rho_hot = rho_hot
        self.rho_cold = rho_cold
        self.gossip_interval = gossip_interval
        self.hop_count = hop_count
        self.gossip_r = gossip_r
        self.gossip_discount = gossip_discount
        self.gossip_shards_per_node = gossip_shards_per_node
        self.exploit_probability = exploit_probability
        self.latency_exponent = latency_exponent
        self.hot_shards: set[int] = set()
        # pheromone[node][shard_id][neighbor] = unnormalized non-negative weight
        self.pheromone: dict[int, dict[int, dict[int, float]]] = {n: {} for n in node_ids}

    # -- per-tick maintenance -------------------------------------------------

    def on_tick(self, tick: int) -> float:
        g = self.graph
        control_cost = 0.0
        self._evaporate()

        alive_nodes = [n for n in self.node_ids if is_alive(g, n)]
        if not alive_nodes:
            return control_cost
        # True average probe budget.  Forcing one probe every tick made the
        # control plane dominate quiet periods and made the reported overhead
        # insensitive to probe_frac on small topologies.
        n_probes = int(self._rng.binomial(len(alive_nodes), self.probe_frac))
        launch_nodes = self._rng.choice(alive_nodes, size=n_probes, replace=False)
        for src in launch_nodes:
            target_shard = int(self._rng.integers(0, self.shard_map.n_shards))
            path, cost, success = self._run_probe(g, int(src), target_shard)
            control_cost += len(path)  # one ant message per hop traversed
            if success:
                self._reinforce(path, target_shard, cost)

        if tick > 0 and tick % self.gossip_interval == 0 and self.hop_count > 0:
            control_cost += self._gossip_round(g)

        return control_cost

    def _evaporate(self) -> None:
        for node, per_shard in self.pheromone.items():
            for shard_id, neigh_map in per_shard.items():
                rho = self.rho_hot if shard_id in self.hot_shards else self.rho_cold
                for neighbor in list(neigh_map.keys()):
                    neigh_map[neighbor] *= (1.0 - rho)

    def _run_probe(self, g: nx.Graph, src: int, target_shard: int) -> tuple[list[int], float, bool]:
        owner = self.shard_map.owner(target_shard)
        path = [src]
        cost = 0.0
        current = src
        if current == owner:
            return path, cost, True
        for _ in range(self.max_hops):
            neighbors = [neighbor for neighbor in alive_neighbors(g, current) if neighbor not in path]
            if not neighbors:
                break
            nxt = self._choose_neighbor(g, current, target_shard, neighbors, exploit=False)
            cost += g.edges[current, nxt]["latency"]
            path.append(nxt)
            current = nxt
            if current == owner:
                return path, cost, True
        return path, cost, False

    def _reinforce(self, path: list[int], target_shard: int, cost: float) -> None:
        # quality-weighted, AntNet-style (JAIR98 §4): a path at reference_cost
        # gets full base_r; a longer one decays toward min_reinforcement_frac
        # * base_r rather than to zero, so a working-but-mediocre fallback
        # route stays alive (useful the moment the good route breaks) instead
        # of being starved out entirely.
        quality = self.reference_cost / max(cost, 1e-6)
        r = self.base_r * max(self.min_reinforcement_frac, min(1.0, quality))
        for i in range(len(path) - 1):
            node, nxt = path[i], path[i + 1]
            pher_map = self.pheromone.setdefault(node, {}).setdefault(target_shard, {})
            pher_map[nxt] = pher_map.get(nxt, self._init_pher) + r

    def report_result(self, path: list[int], target_shard: int, success: bool, cost: float) -> None:
        """Completed query paths are useful observations, not read-only
        traffic. This is AntNet-style feedback with no extra control message.
        """
        if success and len(path) > 1:
            self._reinforce(path, target_shard, cost)

    def _gossip_round(self, g: nx.Graph) -> float:
        """Bounded-flood: each alive node shares its best-known direction per
        shard with everything in its hop_count neighborhood, discounted by
        hop distance. hop_count=0 means this method is never called at all
        (see on_tick), which is what makes hop_count a clean sweep down to
        pure AntNet locality.
        """
        control_cost = 0.0
        alive_nodes = [n for n in self.node_ids if is_alive(g, n)]
        for u in alive_nodes:
            pher_u = self.pheromone.get(u, {})
            if not pher_u:
                continue
            # A real control plane has a fixed bandwidth budget.  Shipping a
            # node's entire shard table was the dominant cost in the first
            # implementation and makes gossip uncompetitive by construction.
            # Send only the strongest (most recently reinforced) directions;
            # repeated rounds eventually cover cold shards without flooding.
            advertised = sorted(
                ((shard_id, max(neigh_map.values())) for shard_id, neigh_map in pher_u.items() if neigh_map),
                key=lambda item: item[1], reverse=True,
            )[: self.gossip_shards_per_node]
            if not advertised:
                continue
            paths = nx.single_source_shortest_path(g, u, cutoff=self.hop_count)
            for v, path in paths.items():
                if v == u:
                    continue
                hops = len(path) - 1
                hop_toward_u = path[-2]
                control_cost += 1.0
                for shard_id, best_val in advertised:
                    strength = self.gossip_r * (self.gossip_discount ** hops) * best_val
                    v_map = self.pheromone.setdefault(v, {}).setdefault(shard_id, {})
                    v_map[hop_toward_u] = v_map.get(hop_toward_u, self._init_pher) + strength
        return control_cost

    # -- routing decision -------------------------------------------------

    def _choose_neighbor(self, g, current_node, target_shard, neighbors, exploit: bool) -> int:
        """Blend learned pheromone with immediately observable link latency.

        Probes sample the score distribution; live queries exploit it with a
        small escape probability. This separates learning from serving and
        avoids routing every real query as an exploratory random walk.
        """
        pher_map = self.pheromone.setdefault(current_node, {}).setdefault(target_shard, {})
        pheromone = np.array([max(0.0, pher_map.get(nb, self._init_pher)) for nb in neighbors], dtype=float)
        latency = np.array([g.edges[current_node, nb]["latency"] for nb in neighbors], dtype=float)
        scores = (pheromone + 1e-12) / np.power(np.maximum(latency, 1e-9), self.latency_exponent)
        if exploit and self._rng.random() < self.exploit_probability:
            return int(neighbors[int(np.argmax(scores))])
        weights = scores / scores.sum()
        return int(neighbors[int(self._rng.choice(len(neighbors), p=weights))])

    def next_hop(self, current_node: int, target_shard: int, visited: set[int] | None = None) -> int | None:
        g = self.graph
        if not is_alive(g, current_node):
            return None
        neighbors = [neighbor for neighbor in alive_neighbors(g, current_node) if not visited or neighbor not in visited]
        if not neighbors:
            return None
        return self._choose_neighbor(g, current_node, target_shard, neighbors, exploit=True)
