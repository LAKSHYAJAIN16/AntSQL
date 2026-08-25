"""Adversarial disruption events: node death/revival, shard migration,
link-latency shift, workload shift — the four arms from the original
brief ("destroy nodes, move shards, alter latencies, change the
workload"), each an independent Poisson process whose rate scales with a
single churn_rate knob (this IS lambda in related_work.md's H1).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

import networkx as nx
import numpy as np


@dataclass
class ChurnEvent:
    kind: str  # 'node_death' | 'node_revival' | 'shard_migration' | 'latency_shift' | 'workload_shift'
    tick: int
    detail: dict


class ChurnScheduler:
    """churn_rate is events/tick, split evenly across the four arms unless
    per-arm weights are given. Node death only affects currently-alive
    nodes not already dead; revival only affects currently-dead nodes, so
    at high churn_rate the system reaches a dynamic equilibrium of
    simultaneously-dead nodes rather than eventually killing everything.
    """

    def __init__(
        self,
        graph: nx.Graph,
        n_shards: int,
        churn_rate: float,
        seed: int,
        arm_weights: dict[str, float] | None = None,
    ):
        self.graph = graph
        self.n_shards = n_shards
        self.churn_rate = churn_rate
        self._rng = np.random.default_rng(seed)
        self._dead_nodes: set[int] = set()
        weights = arm_weights or {
            "node_death": 0.25,
            "node_revival": 0.25,
            "shard_migration": 0.25,
            "latency_shift": 0.15,
            "workload_shift": 0.10,
        }
        total = sum(weights.values())
        self._arm_rates = {k: churn_rate * v / total for k, v in weights.items()}

    def step(self, tick: int) -> list[ChurnEvent]:
        events: list[ChurnEvent] = []
        for kind, rate in self._arm_rates.items():
            n = self._rng.poisson(rate)
            for _ in range(n):
                ev = self._make_event(kind, tick)
                if ev is not None:
                    events.append(ev)
        return events

    def _make_event(self, kind: str, tick: int) -> ChurnEvent | None:
        nodes = list(self.graph.nodes())
        if kind == "node_death":
            candidates = [n for n in nodes if n not in self._dead_nodes]
            if not candidates:
                return None
            victim = int(self._rng.choice(candidates))
            self._dead_nodes.add(victim)
            self.graph.nodes[victim]["alive"] = False
            return ChurnEvent(kind, tick, {"node": victim})
        if kind == "node_revival":
            if not self._dead_nodes:
                return None
            survivor = int(self._rng.choice(list(self._dead_nodes)))
            self._dead_nodes.discard(survivor)
            self.graph.nodes[survivor]["alive"] = True
            return ChurnEvent(kind, tick, {"node": survivor})
        if kind == "shard_migration":
            shard = int(self._rng.integers(0, self.n_shards))
            new_owner = int(self._rng.choice(nodes))
            return ChurnEvent(kind, tick, {"shard": shard, "new_owner": new_owner})
        if kind == "latency_shift":
            edges = list(self.graph.edges())
            if not edges:
                return None
            u, v = edges[int(self._rng.integers(0, len(edges)))]
            factor = float(self._rng.uniform(0.3, 3.0))
            self.graph.edges[u, v]["latency"] *= factor
            return ChurnEvent(kind, tick, {"edge": (u, v), "factor": factor})
        if kind == "workload_shift":
            new_hot = list(self._rng.choice(self.n_shards, size=min(3, self.n_shards), replace=False))
            return ChurnEvent(kind, tick, {"new_hot_shards": [int(s) for s in new_hot]})
        return None
