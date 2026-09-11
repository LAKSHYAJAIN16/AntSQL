"""Orchestration: build a fresh (topology, shards, workload, churn,
strategy) per trial from a seed, run it, collect one summary row. Sweeping
churn_rate across strategies is exactly the H1 crossover experiment from
related_work.md §0.4.
"""
from __future__ import annotations

import itertools
import hashlib

import pandas as pd

from antsql_sim.churn import ChurnScheduler
from antsql_sim.routing import STRATEGIES
from antsql_sim.shards import ShardMap
from antsql_sim.simulator import Simulator
from antsql_sim.topology import generate_topology
from antsql_sim.workload import WorkloadGenerator


def run_condition(
    strategy_name: str,
    churn_rate: float,
    seed: int,
    ticks: int,
    n_nodes: int,
    n_shards: int,
    arrival_rate_per_node: float = 0.5,
    strategy_kwargs: dict | None = None,
    churn_arm_weights: dict[str, float] | None = None,
) -> dict:
    graph = generate_topology("nsfnet_like", n=n_nodes, seed=seed)
    node_ids = list(graph.nodes())
    shard_map = ShardMap(n_shards=n_shards, node_ids=node_ids, seed=seed + 1)
    workload = WorkloadGenerator(
        node_ids=node_ids, n_shards=n_shards, arrival_rate_per_node=arrival_rate_per_node, seed=seed + 2
    )
    churn = ChurnScheduler(
        graph=graph, n_shards=n_shards, churn_rate=churn_rate, seed=seed + 3,
        arm_weights=churn_arm_weights,
    )

    strategy_cls = STRATEGIES[strategy_name]
    strategy = strategy_cls(graph, shard_map, node_ids, seed=seed + 4, **(strategy_kwargs or {}))

    sim = Simulator(graph=graph, shard_map=shard_map, workload=workload, churn=churn, strategy=strategy)
    metrics = sim.run(ticks)

    row = {
        "strategy": strategy_name,
        "churn_rate": churn_rate,
        "seed": seed,
        "n_nodes": n_nodes,
        "n_shards": n_shards,
        "ticks": ticks,
    }
    row.update(metrics.summary())
    return row


def run_sweep(
    strategy_names: list[str],
    churn_rates: list[float],
    trials: int,
    ticks: int,
    n_nodes: int,
    n_shards: int,
    base_seed: int = 0,
    arrival_rate_per_node: float = 0.5,
    strategy_kwargs_by_name: dict[str, dict] | None = None,
) -> pd.DataFrame:
    strategy_kwargs_by_name = strategy_kwargs_by_name or {}
    rows = []
    combos = list(itertools.product(strategy_names, churn_rates, range(trials)))
    for strategy_name, churn_rate, trial in combos:
        # Built-in hash() is deliberately salted for each Python process.
        # A stable digest keeps a paper's sweep exactly reproducible.
        key = f"{base_seed}|{strategy_name}|{churn_rate:.12g}|{trial}".encode()
        seed = base_seed + int.from_bytes(hashlib.sha256(key).digest()[:4], "big") % 1_000_000
        row = run_condition(
            strategy_name=strategy_name,
            churn_rate=churn_rate,
            seed=seed,
            ticks=ticks,
            n_nodes=n_nodes,
            n_shards=n_shards,
            arrival_rate_per_node=arrival_rate_per_node,
            strategy_kwargs=strategy_kwargs_by_name.get(strategy_name),
        )
        row["trial"] = trial
        rows.append(row)
    return pd.DataFrame(rows)
