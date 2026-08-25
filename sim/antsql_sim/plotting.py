"""The headline plot: recovery time and p90 query cost vs. churn rate, one
line per strategy — this IS the crossover plot from related_work.md §0.4's
H1. If AntSQL's line crosses below adaptive_centralized's line as
churn_rate increases, that's the result the whole project is looking for.
"""
from __future__ import annotations

import matplotlib.pyplot as plt
import pandas as pd


def plot_crossover(df: pd.DataFrame, out_path: str) -> None:
    agg = df.groupby(["strategy", "churn_rate"]).mean(numeric_only=True).reset_index()

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    for strategy, group in agg.groupby("strategy"):
        group = group.sort_values("churn_rate")
        axes[0].plot(group["churn_rate"], group["mean_recovery_ticks"], marker="o", label=strategy)
        axes[1].plot(group["churn_rate"], group["p90_cost"], marker="o", label=strategy)

    axes[0].set_xlabel("churn rate (events/tick)")
    axes[0].set_ylabel("mean recovery time (ticks)")
    axes[0].set_title("Recovery time vs. churn rate")
    axes[0].legend()

    axes[1].set_xlabel("churn rate (events/tick)")
    axes[1].set_ylabel("p90 query cost")
    axes[1].set_title("Steady-state query cost vs. churn rate")
    axes[1].legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
