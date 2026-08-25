"""Reproducible AntSQL control-plane calibration.

Runs a small zero-churn grid before the main resilience study.  The aim is
not to select the lowest latency configuration in isolation: it identifies
settings with a reasonable control-overhead budget and then reports the
speed/precision trade-off introduced by bounded-hop gossip.
"""
from __future__ import annotations

import itertools
import os

import pandas as pd

from antsql_sim.experiment import run_condition

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "..", "results")


def main() -> None:
    rows = []
    grid = itertools.product(
        [0.05, 0.10],             # probe fraction
        [0, 1, 2],                # gossip hops; zero is pure locality
        [0.0, 0.01],              # gossip reinforcement magnitude
    )
    for config_id, (probe_frac, hop_count, gossip_r) in enumerate(grid):
        # Gossip weight has no meaning at zero hops; keep one canonical row.
        if hop_count == 0 and gossip_r != 0.0:
            continue
        kwargs = {"probe_frac": probe_frac, "hop_count": hop_count, "gossip_r": gossip_r}
        # Screening pass: one fixed seed and a 150-tick horizon. Promote the
        # best few candidates to a multi-seed 500+ tick study afterwards.
        for trial in range(1):
            row = run_condition(
                "antsql", 0.0, seed=10_000 + config_id * 100 + trial,
                ticks=150, n_nodes=14, n_shards=10, strategy_kwargs=kwargs,
            )
            row.update(kwargs)
            row["trial"] = trial
            rows.append(row)

    df = pd.DataFrame(rows)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    path = os.path.join(RESULTS_DIR, "calibration.csv")
    df.to_csv(path, index=False)
    cols = ["probe_frac", "hop_count", "gossip_r"]
    measures = ["success_rate", "p90_cost", "control_overhead_ratio", "throughput_per_tick"]
    print(df.groupby(cols)[measures].mean().sort_values(["p90_cost", "control_overhead_ratio"]).round(3))
    print(f"Wrote {path} ({len(df)} rows)")


if __name__ == "__main__":
    main()
