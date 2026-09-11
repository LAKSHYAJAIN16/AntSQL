"""Tests the hypothesis raised in antsql_paper.md section 7/9: does the
churn-0.30 reversal (AntSQL loses to adaptive-centralized routing at severe
compound churn, after winning at churn 0.10-0.20) shrink or grow as the
query routing hop budget (Simulator.max_query_hops) changes? A path needs
every intermediate hop alive simultaneously; if that requirement hurts
AntSQL specifically, a smaller hop budget (forcing shorter, more-likely-
intact paths) should narrow the gap, and a larger one should widen it.

  python run_hop_budget_sweep.py            default: churn 0.2 and 0.3
  python run_hop_budget_sweep.py --quick     fast smoke test

Output: results/hop_budget_sweep.csv, one row per
(strategy, churn_rate, max_query_hops, seed).
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import os

import pandas as pd

from antsql_sim.experiment import run_condition

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "..", "results")
STRATEGIES = ["adaptive_centralized", "antsql"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true", help="fast smoke test")
    parser.add_argument("--nodes", type=int, default=14)
    parser.add_argument("--shards", type=int, default=10)
    parser.add_argument("--ticks", type=int, default=None)
    parser.add_argument("--trials", type=int, default=None)
    parser.add_argument("--churn-rates", type=float, nargs="+", default=None)
    parser.add_argument("--hop-budgets", type=int, nargs="+", default=None)
    parser.add_argument("--arrival-rate", type=float, default=0.5)
    args = parser.parse_args()

    ticks = args.ticks or (200 if args.quick else 400)
    trials = args.trials or (3 if args.quick else 20)
    churn_rates = args.churn_rates or [0.2, 0.3]
    hop_budgets = args.hop_budgets or ([6, 15] if args.quick else [4, 6, 8, 10, 15, 25])

    print(
        f"Running hop-budget sweep: strategies={STRATEGIES} churn_rates={churn_rates} "
        f"hop_budgets={hop_budgets} trials={trials} ticks={ticks} "
        f"n_nodes={args.nodes} n_shards={args.shards}"
    )

    rows = []
    combos = list(itertools.product(STRATEGIES, churn_rates, hop_budgets, range(trials)))
    for strategy_name, churn_rate, hop_budget, trial in combos:
        key = f"hop_budget|{strategy_name}|{churn_rate:.12g}|{hop_budget}|{trial}".encode()
        seed = int.from_bytes(hashlib.sha256(key).digest()[:4], "big") % 1_000_000
        row = run_condition(
            strategy_name=strategy_name,
            churn_rate=churn_rate,
            seed=seed,
            ticks=ticks,
            n_nodes=args.nodes,
            n_shards=args.shards,
            arrival_rate_per_node=args.arrival_rate,
            max_query_hops=hop_budget,
        )
        row["max_query_hops"] = hop_budget
        row["trial"] = trial
        rows.append(row)

    df = pd.DataFrame(rows)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, "hop_budget_sweep.csv")
    df.to_csv(csv_path, index=False)
    print(f"Wrote {csv_path} ({len(df)} rows)")

    summary = (
        df.groupby(["churn_rate", "max_query_hops", "strategy"])["success_rate"]
        .mean().unstack("strategy").round(3)
    )
    summary["antsql_minus_adaptive"] = summary["antsql"] - summary["adaptive_centralized"]
    pd.set_option("display.width", 200)
    print(summary)


if __name__ == "__main__":
    main()
