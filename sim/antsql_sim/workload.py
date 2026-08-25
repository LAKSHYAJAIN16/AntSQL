"""Query arrival process.

Mirrors AntNet's temporal x spatial traffic model (JAIR98 §6.2) but with
shards standing in for destination nodes: Poisson arrivals per source node,
spatial distribution over target shards is either uniform or hot-spot
weighted, and the hot-spot set can shift at runtime (a workload-churn
event — see churn.py).
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np


@dataclass
class Query:
    source: int
    target_shard: int
    born_tick: int


@dataclass
class WorkloadGenerator:
    node_ids: list[int]
    n_shards: int
    arrival_rate_per_node: float  # expected queries/tick/node
    seed: int
    hot_shards: list[int] = field(default_factory=list)
    hot_shard_weight: float = 0.7  # fraction of mass on hot_shards when non-empty

    def __post_init__(self):
        self._rng = np.random.default_rng(self.seed)

    def shift_hot_shards(self, new_hot_shards: list[int]) -> None:
        self.hot_shards = new_hot_shards

    def step(self, tick: int) -> list[Query]:
        queries: list[Query] = []
        for node in self.node_ids:
            n_arrivals = self._rng.poisson(self.arrival_rate_per_node)
            for _ in range(n_arrivals):
                target = self._pick_target_shard()
                queries.append(Query(source=node, target_shard=target, born_tick=tick))
        return queries

    def _pick_target_shard(self) -> int:
        if self.hot_shards and self._rng.random() < self.hot_shard_weight:
            return int(self._rng.choice(self.hot_shards))
        return int(self._rng.integers(0, self.n_shards))
