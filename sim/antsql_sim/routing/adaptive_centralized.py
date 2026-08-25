"""The baseline that actually matters (related_work.md §0.4): a coordinator
that periodically recomputes globally-optimal routing tables from true
current state, then broadcasts them out. Even with omniscient access to
ground truth at recompute time, there's a real staleness window between
(a) reopt_interval, how often it looks, and (b) broadcast_latency, how
long the fix takes to land everywhere — this is the mechanism a fair
"decentralization helps under churn" claim has to beat, not a strawman
that never adapts at all.
"""
from __future__ import annotations

from antsql_sim.routing.base import RoutingStrategy, dijkstra_tables

CONTROL_MSG_COST = 1.0  # abstract units per node reached by a broadcast


class AdaptiveCentralized(RoutingStrategy):
    name = "adaptive_centralized"

    def __init__(self, graph, shard_map, node_ids, seed, reopt_interval=20, broadcast_latency=3):
        super().__init__(graph, shard_map, node_ids, seed)
        self.reopt_interval = reopt_interval
        self.broadcast_latency = broadcast_latency
        self._live_tables = dijkstra_tables(graph, shard_map, node_ids)
        self._pending_tables = None
        self._pending_ready_tick = None

    def on_tick(self, tick):
        control_cost = 0.0
        if tick > 0 and tick % self.reopt_interval == 0:
            self._pending_tables = dijkstra_tables(self.graph, self.shard_map, self.node_ids)
            self._pending_ready_tick = tick + self.broadcast_latency
            control_cost += len(self.node_ids) * CONTROL_MSG_COST
        if self._pending_tables is not None and tick >= self._pending_ready_tick:
            self._live_tables = self._pending_tables
            self._pending_tables = None
        return control_cost

    def next_hop(self, current_node, target_shard, visited=None):
        nxt = self._live_tables.get(current_node, {}).get(target_shard)
        return None if visited and nxt in visited else nxt
