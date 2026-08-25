"""Large-network AntSQL stress run (defaults: 1,000 sites, 100 shards)."""
from __future__ import annotations

import argparse
import os

from antsql_sim.experiment import run_sweep


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=int, default=1_000)
    parser.add_argument("--shards", type=int, default=100)
    parser.add_argument("--ticks", type=int, default=100)
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--arrival-rate", type=float, default=0.02)
    args = parser.parse_args()
    df = run_sweep(
        strategy_names=["antsql"], churn_rates=[0.0, 0.05, 0.20],
        trials=args.trials, ticks=args.ticks, n_nodes=args.nodes, n_shards=args.shards,
        arrival_rate_per_node=args.arrival_rate,
    )
    path = os.path.join(os.path.dirname(__file__), "..", "results", f"scale_{args.nodes}.csv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    df.to_csv(path, index=False)
    print(df.groupby("churn_rate")[["success_rate", "throughput_per_tick", "p90_cost", "control_overhead_ratio"]].mean().round(3))
    print(f"Wrote {path}")


if __name__ == "__main__":
    main()
