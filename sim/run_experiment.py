"""CLI entry point.

  python run_experiment.py --quick    fast smoke test
  python run_experiment.py            full default sweep

Output: results/summary.csv and results/crossover.png (mean recovery time
and p90 query cost vs. churn rate, one line per strategy — related_work.md
§0.4's H1 plot).
"""
from __future__ import annotations

import argparse
import os

from antsql_sim.experiment import run_sweep
from antsql_sim.plotting import plot_crossover

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "..", "results")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true", help="fast smoke test")
    parser.add_argument("--nodes", type=int, default=None)
    parser.add_argument("--shards", type=int, default=None)
    parser.add_argument("--ticks", type=int, default=None)
    parser.add_argument("--trials", type=int, default=None)
    parser.add_argument("--arrival-rate", type=float, default=0.5, help="queries/tick/site")
    parser.add_argument("--output-stem", type=str, default="summary", help="result filename stem in results/")
    parser.add_argument(
        "--churn-rates", type=float, nargs="+", default=None, help="events/tick, e.g. 0 0.02 0.05 0.1 0.2 0.4"
    )
    parser.add_argument(
        "--strategies",
        type=str,
        nargs="+",
        default=["static_centralized", "adaptive_centralized", "decentralized_greedy", "antsql"],
    )
    args = parser.parse_args()

    if args.quick:
        n_nodes = args.nodes or 14
        n_shards = args.shards or 10
        ticks = args.ticks or 200
        trials = args.trials or 2
        churn_rates = args.churn_rates or [0.0, 0.1, 0.3]
    else:
        n_nodes = args.nodes or 14
        n_shards = args.shards or 10
        ticks = args.ticks or 1000
        trials = args.trials or 10
        churn_rates = args.churn_rates or [0.0, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5]

    print(
        f"Running sweep: strategies={args.strategies} churn_rates={churn_rates} "
        f"trials={trials} ticks={ticks} n_nodes={n_nodes} n_shards={n_shards}"
    )

    df = run_sweep(
        strategy_names=args.strategies,
        churn_rates=churn_rates,
        trials=trials,
        ticks=ticks,
        n_nodes=n_nodes,
        n_shards=n_shards,
        arrival_rate_per_node=args.arrival_rate,
    )

    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{args.output_stem}.csv")
    df.to_csv(csv_path, index=False)
    print(f"Wrote {csv_path} ({len(df)} rows)")

    summary_cols = [
        "strategy",
        "churn_rate",
        "success_rate",
        "mean_cost",
        "p90_cost",
        "control_overhead_ratio",
        "mean_recovery_ticks",
        "n_never_recovered",
    ]
    print(df.groupby(["strategy", "churn_rate"])[summary_cols[2:]].mean(numeric_only=True).round(3))

    plot_path = os.path.join(RESULTS_DIR, f"{args.output_stem}.png")
    plot_crossover(df, plot_path)
    print(f"Wrote {plot_path}")


if __name__ == "__main__":
    main()
