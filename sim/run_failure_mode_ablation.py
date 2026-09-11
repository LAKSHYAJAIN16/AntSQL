"""Per-failure-mode ablation: isolate each churn arm (node death, node
revival, shard migration, latency shift, workload shift) instead of the
compound churn used by run_experiment.py's main sweep.

Motivated by antsql_paper.md section 8: the compound-churn study found
AntSQL beats adaptive-centralized routing in a churn window (~0.10-0.20)
but loses again at churn 0.30. This isolates which arm(s) drive that
reversal by running each arm alone, at the same total event rate, across
all four strategies.

  python run_failure_mode_ablation.py            default: churn 0.2 and 0.3
  python run_failure_mode_ablation.py --quick     fast smoke test

Output: results/failure_mode_ablation.csv, one row per
(strategy, churn_rate, arm, seed).
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import os

import pandas as pd

from antsql_sim.experiment import run_condition

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "..", "results")

ARMS = ["node_death", "node_revival", "shard_migration", "latency_shift", "workload_shift"]
STRATEGIES = ["static_centralized", "adaptive_centralized", "decentralized_greedy", "antsql"]


def single_arm_weights(arm: str) -> dict[str, float]:
    # node_death alone with no node_revival would only ever kill nodes, so
    # pair every isolated failure arm with node_revival at the same rate to
    # keep the network in a dynamic equilibrium instead of decaying to zero
    # live nodes over the run — the same reason node_death/node_revival are
    # co-weighted in ChurnScheduler's default split.
    if arm == "node_revival":
        return {"node_death": 0.5, "node_revival": 0.5}
    return {arm: 0.5, "node_revival": 0.5}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true", help="fast smoke test")
    parser.add_argument("--nodes", type=int, default=14)
    parser.add_argument("--shards", type=int, default=10)
    parser.add_argument("--ticks", type=int, default=None)
    parser.add_argument("--trials", type=int, default=None)
    parser.add_argument("--churn-rates", type=float, nargs="+", default=None)
    parser.add_argument("--arrival-rate", type=float, default=0.5)
    args = parser.parse_args()

    ticks = args.ticks or (200 if args.quick else 400)
    trials = args.trials or (3 if args.quick else 20)
    churn_rates = args.churn_rates or [0.2, 0.3]

    print(
        f"Running failure-mode ablation: arms={ARMS} churn_rates={churn_rates} "
        f"trials={trials} ticks={ticks} n_nodes={args.nodes} n_shards={args.shards}"
    )

    rows = []
    combos = list(itertools.product(STRATEGIES, churn_rates, ARMS, range(trials)))
    for strategy_name, churn_rate, arm, trial in combos:
        key = f"failure_mode|{strategy_name}|{churn_rate:.12g}|{arm}|{trial}".encode()
        seed = int.from_bytes(hashlib.sha256(key).digest()[:4], "big") % 1_000_000
        row = run_condition(
            strategy_name=strategy_name,
            churn_rate=churn_rate,
            seed=seed,
            ticks=ticks,
            n_nodes=args.nodes,
            n_shards=args.shards,
            arrival_rate_per_node=args.arrival_rate,
            churn_arm_weights=single_arm_weights(arm),
        )
        row["arm"] = arm
        row["trial"] = trial
        rows.append(row)

    df = pd.DataFrame(rows)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, "failure_mode_ablation.csv")
    df.to_csv(csv_path, index=False)
    print(f"Wrote {csv_path} ({len(df)} rows)")

    summary = df.groupby(["arm", "churn_rate", "strategy"])["success_rate"].mean().unstack("strategy").round(3)
    pd.set_option("display.width", 200)
    print(summary)


if __name__ == "__main__":
    main()
