"""Floor baseline: Dijkstra once at t=0, never updates again — no reaction
to node death, shard migration, latency shift, or workload shift. Exists
to show what "no adaptation at all" costs; adaptive_centralized is the
baseline that actually matters (see related_work.md §0.4).
"""
from __future__ import annotations

from antsql_sim.routing.base import RoutingStrategy, dijkstra_tables


class StaticCentralized(RoutingStrategy):
    name = "static_centralized"

    def __init__(self, graph, shard_map, node_ids, seed):
        super().__init__(graph, shard_map, node_ids, seed)
        self._tables = dijkstra_tables(graph, shard_map, node_ids)

    def next_hop(self, current_node, target_shard, visited=None):
        nxt = self._tables.get(current_node, {}).get(target_shard)
        return None if visited and nxt in visited else nxt
