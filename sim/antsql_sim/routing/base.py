"""Shared interface for the four routing conditions.

Query routing itself (walking hop-by-hop from source to a shard's true
current owner) is common logic, implemented once in simulator.py, which
calls next_hop() repeatedly. Only decision-making, per-tick maintenance,
reaction to churn, and feedback differ per strategy.
"""
from __future__ import annotations

import networkx as nx
import numpy as np

from antsql_sim.shards import ShardMap
from antsql_sim.topology import alive_subgraph


class RoutingStrategy:
    name = "base"

    def __init__(self, graph: nx.Graph, shard_map: ShardMap, node_ids: list[int], seed: int):
        self.graph = graph
        self.shard_map = shard_map
        self.node_ids = node_ids
        self._rng = np.random.default_rng(seed)

    def on_tick(self, tick: int) -> float:
        """Per-tick maintenance (evaporation, gossip, coordinator rounds, ant
        probes...). Returns the control-traffic cost incurred this tick, in
        abstract message units, for overhead accounting."""
        return 0.0

    def on_churn(self) -> None:
        """Hook for strategies that react immediately to a churn event
        rather than only noticing it on the next maintenance tick. No-op
        by default (static/adaptive-centralized never react out-of-band;
        antsql doesn't either — its whole point is it never needs an
        out-of-band signal, staleness is handled by evaporation alone)."""
        pass

    def next_hop(self, current_node: int, target_shard: int, visited: set[int] | None = None) -> int | None:
        raise NotImplementedError

    def report_result(self, path: list[int], target_shard: int, success: bool, cost: float) -> None:
        """Feedback hook for the query that just completed/failed. antsql
        reinforces here; others no-op."""
        pass


def dijkstra_tables(graph: nx.Graph, shard_map: ShardMap, node_ids: list[int]) -> dict[int, dict[int, int]]:
    """{node: {shard_id: next_hop}} computed by Dijkstra to each shard's
    current owner, on whatever graph is passed in (caller decides whether
    that's ground truth or a stale snapshot)."""
    g = alive_subgraph(graph)
    tables: dict[int, dict[int, int]] = {n: {} for n in node_ids}
    for shard_id in range(shard_map.n_shards):
        owner = shard_map.owner(shard_id)
        if owner not in g:
            continue
        try:
            from_owner = nx.single_source_dijkstra_path(g, source=owner, weight="latency")
        except nx.NetworkXError:
            continue
        for dest_node, path_from_owner in from_owner.items():
            path_to_owner = list(reversed(path_from_owner))
            tables[dest_node][shard_id] = path_to_owner[1] if len(path_to_owner) > 1 else owner
    return tables
