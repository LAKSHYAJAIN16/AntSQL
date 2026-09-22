"""Tests the hypothesis raised in antsql_paper.md section 7/9: does the
churn-0.30 reversal (AntSQL loses to adaptive-centralized routing at severe
compound churn, after winning at churn 0.10-0.20) shrink or grow as the
query routing hop budget (Simulator.max_query_hops) changes? A path needs
every intermediate hop alive simultaneously; if that requirement hurts
AntSQL specifically, a smaller hop budget (forcing shorter, more-likely-
intact paths) should narrow the gap, and a larger one should widen it.

Paired/blocked design: unlike experiment.run_sweep and
run_failure_mode_ablation.py, the seed for a given (churn_rate, trial) does
NOT depend on strategy_name or max_query_hops. All strategies and hop
budgets at the same (churn_rate, trial) therefore run against the exact
same topology, shard placement, workload arrival stream, and churn
schedule -- they differ only in routing strategy and hop budget. This is a
randomized complete block design (block = seed), so the strategy-vs-
strategy gap at each hop budget can be computed as a per-block paired
difference instead of a difference of two independent-sample means, which
is the "paired seeds ... variance-reduction design" the original
(unpaired) run of this sweep concluded it needed (antsql_paper.md section
7): per-cell standard deviations there (0.10-0.20) dwarfed the
cross-hop-budget differences in the gap, so the hypothesis could not be
confirmed or ruled out from independent samples alone.

  python run_hop_budget_sweep.py            default: churn 0.2 and 0.3
  python run_hop_budget_sweep.py --quick     fast smoke test

Output: results/hop_budget_sweep_paired.csv, one row per
(strategy, churn_rate, max_query_hops, seed); and
results/hop_budget_sweep_paired_summary.csv, one row per
(churn_rate, max_query_hops) with the paired antsql-minus-adaptive gap,
its standard error, and a normal-approximation 95% CI. The original
independent-seed run (results/hop_budget_sweep.csv) is left untouched as
the earlier, unpaired result it was.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import os

import numpy as np
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
        f"Running PAIRED hop-budget sweep: strategies={STRATEGIES} churn_rates={churn_rates} "
        f"hop_budgets={hop_budgets} trials={trials} ticks={ticks} "
        f"n_nodes={args.nodes} n_shards={args.shards}"
    )

    rows = []
    # Loop order doesn't affect the seed (it depends only on churn_rate and
    # trial), but iterating trial outermost makes the blocking explicit: all
    # strategy/hop-budget combinations for one trial share one seed.
    combos = list(itertools.product(churn_rates, range(trials), hop_budgets, STRATEGIES))
    for churn_rate, trial, hop_budget, strategy_name in combos:
        key = f"hop_budget_paired|{churn_rate:.12g}|{trial}".encode()
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
    csv_path = os.path.join(RESULTS_DIR, "hop_budget_sweep_paired.csv")
    df.to_csv(csv_path, index=False)
    print(f"Wrote {csv_path} ({len(df)} rows)")

    # Per-block paired difference: for each (churn_rate, hop_budget, trial),
    # both strategies ran on the identical seed, so antsql - adaptive is a
    # matched-pairs difference, not a difference of independent means.
    pivot = df.pivot_table(
        index=["churn_rate", "max_query_hops", "trial"],
        columns="strategy",
        values="success_rate",
    )
    pivot["gap"] = pivot["antsql"] - pivot["adaptive_centralized"]

    summary_rows = []
    for (churn_rate, hop_budget), group in pivot.groupby(level=["churn_rate", "max_query_hops"]):
        gaps = group["gap"].to_numpy()
        n = len(gaps)
        mean_gap = float(np.mean(gaps))
        se_gap = float(np.std(gaps, ddof=1) / np.sqrt(n)) if n > 1 else float("nan")
        # Normal approximation (not an exact t-interval) -- fine at n=20 for
        # flagging whether the paired gap plausibly differs from zero, which
        # is all this sweep is trying to establish.
        ci_lo, ci_hi = mean_gap - 1.96 * se_gap, mean_gap + 1.96 * se_gap
        summary_rows.append({
            "churn_rate": churn_rate,
            "max_query_hops": hop_budget,
            "n_pairs": n,
            "mean_gap_antsql_minus_adaptive": round(mean_gap, 4),
            "se_gap": round(se_gap, 4),
            "ci95_lo": round(ci_lo, 4),
            "ci95_hi": round(ci_hi, 4),
            "excludes_zero": bool(ci_lo > 0 or ci_hi < 0),
        })

    summary_df = pd.DataFrame(summary_rows).sort_values(["churn_rate", "max_query_hops"])
    summary_csv_path = os.path.join(RESULTS_DIR, "hop_budget_sweep_paired_summary.csv")
    summary_df.to_csv(summary_csv_path, index=False)
    print(f"Wrote {summary_csv_path} ({len(summary_df)} rows)")

    pd.set_option("display.width", 200)
    print(summary_df.to_string(index=False))


if __name__ == "__main__":
    main()
