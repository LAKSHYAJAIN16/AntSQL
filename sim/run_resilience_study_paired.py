"""Paired rerun of the main resilience study (antsql_paper.md section 5,
`results/resilience_study_v2.csv`), motivated by a finding from the paired
hop-budget rerun (section 7): `experiment.run_sweep`, used to produce
resilience_study_v2, derives each trial's seed from a hash that includes
`strategy_name`. Every strategy therefore ran on an independently-drawn
topology, shard placement, workload arrival stream, and churn schedule even
"at the same trial" -- none of the section 5 strategy-vs-strategy
comparisons were paired. The paired hop-budget rerun, at the same hop
budget (15) the main study uses, found AntSQL statistically ahead of
adaptive-centralized at both churn 0.20 and 0.30 -- including at 0.30,
where the unpaired main study reported AntSQL *behind* (0.377 vs 0.514).
That is reason enough to question whether the "reversal at churn 0.30" is
a real strategy effect or unpaired-sampling noise, rather than assume it.

This script reruns the exact resilience_study_v2 configuration (14 nodes,
10 shards, 400 ticks, 20 seeds, churn rates 0.00/0.05/0.10/0.20/0.30, all
four strategies) with one change: the seed for a given (churn_rate, trial)
does not depend on strategy_name, so all four strategies at the same trial
share one topology/shard-placement/workload/churn realization and differ
only in routing strategy. That makes every strategy-vs-strategy gap at
each churn rate a matched-pairs difference.

  python run_resilience_study_paired.py            full rerun (~400 sim runs)
  python run_resilience_study_paired.py --quick     fast smoke test

Output: results/resilience_study_v3_paired.csv (raw, one row per
strategy/churn_rate/trial) and
results/resilience_study_v3_paired_summary.csv (paired antsql-minus-X gaps
per churn rate, with standard error and a normal-approximation 95% CI).
resilience_study_v2.csv is left untouched.
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
STRATEGIES = ["static_centralized", "adaptive_centralized", "decentralized_greedy", "antsql"]
# The comparisons antsql_paper.md section 5-6 actually draws conclusions
# from: the crossover-window claim (vs. adaptive-centralized) and the
# stigmergic-credit-assignment claim (vs. decentralized-greedy).
GAP_COMPARISONS = [("antsql", "adaptive_centralized"), ("antsql", "decentralized_greedy")]


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
    churn_rates = args.churn_rates or [0.0, 0.05, 0.1, 0.2, 0.3]

    print(
        f"Running PAIRED resilience study: strategies={STRATEGIES} churn_rates={churn_rates} "
        f"trials={trials} ticks={ticks} n_nodes={args.nodes} n_shards={args.shards}"
    )

    rows = []
    combos = list(itertools.product(churn_rates, range(trials), STRATEGIES))
    for churn_rate, trial, strategy_name in combos:
        key = f"resilience_paired|{churn_rate:.12g}|{trial}".encode()
        seed = int.from_bytes(hashlib.sha256(key).digest()[:4], "big") % 1_000_000
        row = run_condition(
            strategy_name=strategy_name,
            churn_rate=churn_rate,
            seed=seed,
            ticks=ticks,
            n_nodes=args.nodes,
            n_shards=args.shards,
            arrival_rate_per_node=args.arrival_rate,
        )
        row["trial"] = trial
        rows.append(row)

    df = pd.DataFrame(rows)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, "resilience_study_v3_paired.csv")
    df.to_csv(csv_path, index=False)
    print(f"Wrote {csv_path} ({len(df)} rows)")

    pivot = df.pivot_table(index=["churn_rate", "trial"], columns="strategy", values="success_rate")

    summary_rows = []
    for churn_rate, group in pivot.groupby(level="churn_rate"):
        row = {"churn_rate": churn_rate, "n_pairs": len(group)}
        for name in STRATEGIES:
            row[f"mean_{name}"] = round(float(group[name].mean()), 4)
        for a, b in GAP_COMPARISONS:
            gaps = (group[a] - group[b]).to_numpy()
            n = len(gaps)
            mean_gap = float(np.mean(gaps))
            se_gap = float(np.std(gaps, ddof=1) / np.sqrt(n)) if n > 1 else float("nan")
            ci_lo, ci_hi = mean_gap - 1.96 * se_gap, mean_gap + 1.96 * se_gap
            prefix = f"gap_{a}_minus_{b}"
            row[f"{prefix}_mean"] = round(mean_gap, 4)
            row[f"{prefix}_se"] = round(se_gap, 4)
            row[f"{prefix}_ci95_lo"] = round(ci_lo, 4)
            row[f"{prefix}_ci95_hi"] = round(ci_hi, 4)
            row[f"{prefix}_excludes_zero"] = bool(ci_lo > 0 or ci_hi < 0)
        summary_rows.append(row)

    summary_df = pd.DataFrame(summary_rows).sort_values("churn_rate")
    summary_csv_path = os.path.join(RESULTS_DIR, "resilience_study_v3_paired_summary.csv")
    summary_df.to_csv(summary_csv_path, index=False)
    print(f"Wrote {summary_csv_path} ({len(summary_df)} rows)")

    pd.set_option("display.width", 250)
    display_cols = ["churn_rate", "n_pairs"] + [f"mean_{name}" for name in STRATEGIES] + [
        col for a, b in GAP_COMPARISONS
        for col in (f"gap_{a}_minus_{b}_mean", f"gap_{a}_minus_{b}_ci95_lo",
                    f"gap_{a}_minus_{b}_ci95_hi", f"gap_{a}_minus_{b}_excludes_zero")
    ]
    print(summary_df[display_cols].to_string(index=False))


if __name__ == "__main__":
    main()
