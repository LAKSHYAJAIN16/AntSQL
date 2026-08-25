"""Ablation (related_work.md §0.4): decentralized but non-stigmergic —
Bonfils & Bonnet (IPSN 2003) style greedy local hill-climbing. Each node
keeps exactly one belief per shard (next_hop, believed_cost), overwritten
whenever a sampled neighbor exchange finds something that looks better.
No persistent trail, no memory of a second-best option, no evaporation.

This exists to isolate *why* AntSQL might win: if AntSQL beats this too,
the win isn't just "decentralization helps" — it's specifically the
stigmergic (reinforced, multi-path, evaporating) credit assignment that
helps, because this strategy is decentralized without it.
"""
from __future__ import annotations

from antsql_sim.routing.base import RoutingStrategy
from antsql_sim.topology import alive_subgraph


class DecentralizedGreedy(RoutingStrategy):
    name = "decentralized_greedy"

    def __init__(self, graph, shard_map, node_ids, seed, updates_per_tick_frac=0.3):
        super().__init__(graph, shard_map, node_ids, seed)
        self.updates_per_tick_frac = updates_per_tick_frac
        # belief[node][shard_id] = (next_hop, believed_cost)
        self.belief: dict[int, dict[int, tuple[int, float]]] = {n: {} for n in node_ids}
        for shard_id in range(shard_map.n_shards):
            owner = shard_map.owner(shard_id)
            self.belief[owner][shard_id] = (owner, 0.0)

    def on_tick(self, tick):
        g = alive_subgraph(self.graph)
        control_cost = 0.0
        nodes = [n for n in self.node_ids if n in g]
        if not nodes:
            return control_cost
        sample_size = max(1, int(len(nodes) * self.updates_per_tick_frac))
        sampled = self._rng.choice(nodes, size=min(sample_size, len(nodes)), replace=False)

        for node in sampled:
            for neighbor in g.neighbors(node):
                edge_cost = g.edges[node, neighbor]["latency"]
                control_cost += 1.0
                for shard_id, (_, n_cost) in self.belief.get(neighbor, {}).items():
                    candidate_cost = n_cost + edge_cost
                    cur = self.belief[node].get(shard_id)
                    if cur is None or candidate_cost < cur[1]:
                        self.belief[node][shard_id] = (neighbor, candidate_cost)

        # The true owner always knows itself with cost 0 — same "ground truth
        # anchor at the source" treatment antsql gets, so the comparison
        # between the two isolates credit-assignment quality, not whether
        # either strategy even notices a migration happened.
        for shard_id in range(self.shard_map.n_shards):
            owner = self.shard_map.owner(shard_id)
            if owner in g:
                self.belief.setdefault(owner, {})[shard_id] = (owner, 0.0)

        return control_cost

    def next_hop(self, current_node, target_shard, visited=None):
        entry = self.belief.get(current_node, {}).get(target_shard)
        if not entry or (visited and entry[0] in visited):
            return None
        return entry[0]
