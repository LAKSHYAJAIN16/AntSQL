"""Metrics collection, mirroring AntNet's chosen metrics where they map
cleanly (related_work.md §2a §6.3): throughput, a percentile delay stat
rather than mean/variance (delay is heavy-tailed here too), and a control-
overhead ratio. Adds what AntNet's JAIR98 paper didn't need because it
never tested failure (§2a's correction): per-disruption recovery time.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

DISRUPTIVE_KINDS = {"node_death", "shard_migration", "latency_shift", "workload_shift"}


@dataclass
class MetricsCollector:
    ticks: int
    query_ticks: list[int] = field(default_factory=list)
    query_success: list[bool] = field(default_factory=list)
    query_cost: list[float] = field(default_factory=list)  # NaN-free; only recorded for attempted queries
    control_cost_per_tick: list[float] = field(default_factory=list)
    disruption_ticks: list[int] = field(default_factory=list)

    def __post_init__(self):
        self.control_cost_per_tick = [0.0] * self.ticks

    def record_query(self, tick: int, success: bool, cost: float) -> None:
        self.query_ticks.append(tick)
        self.query_success.append(success)
        self.query_cost.append(cost)

    def record_control_cost(self, tick: int, cost: float) -> None:
        if 0 <= tick < self.ticks:
            self.control_cost_per_tick[tick] += cost

    def record_churn_events(self, events) -> None:
        for ev in events:
            if ev.kind in DISRUPTIVE_KINDS:
                self.disruption_ticks.append(ev.tick)

    def _rolling_success_cost(self, window: int = 15) -> np.ndarray:
        """Rolling mean cost of *successful* queries, one value per tick.
        Ticks with no successful queries in-window carry forward the last
        known value (avoids spurious zeros reading as 'recovered')."""
        per_tick_costs: list[list[float]] = [[] for _ in range(self.ticks)]
        for t, ok, c in zip(self.query_ticks, self.query_success, self.query_cost):
            if ok and 0 <= t < self.ticks:
                per_tick_costs[t].append(c)

        series = np.full(self.ticks, np.nan)
        for t in range(self.ticks):
            lo = max(0, t - window + 1)
            window_vals = [v for bucket in per_tick_costs[lo : t + 1] for v in bucket]
            if window_vals:
                series[t] = float(np.mean(window_vals))
        # forward-fill NaNs (ticks with zero successful queries in-window)
        last = np.nan
        for t in range(self.ticks):
            if np.isnan(series[t]):
                series[t] = last
            else:
                last = series[t]
        return series

    def recovery_times(self, tolerance: float = 0.20, baseline_window: int = 30, search_horizon: int = 150) -> list[float | None]:
        """For each disruptive churn event, ticks until rolling cost drops
        back within `tolerance` of its pre-event baseline. None if it never
        does within search_horizon ticks (treated as non-recovery, not
        excluded — a paper reporting only the events that happened to
        recover would be cherry-picking)."""
        series = self._rolling_success_cost()
        results: list[float | None] = []
        for t0 in self.disruption_ticks:
            lo = max(0, t0 - baseline_window)
            baseline_vals = series[lo:t0]
            baseline_vals = baseline_vals[~np.isnan(baseline_vals)]
            if len(baseline_vals) == 0:
                continue
            baseline = float(np.mean(baseline_vals))
            threshold = baseline * (1 + tolerance)
            recovered_at = None
            for dt in range(search_horizon):
                t = t0 + dt
                if t >= self.ticks or np.isnan(series[t]):
                    continue
                if series[t] <= threshold:
                    recovered_at = dt
                    break
            results.append(float(recovered_at) if recovered_at is not None else None)
        return results

    def summary(self) -> dict:
        success = np.array(self.query_success, dtype=bool)
        cost = np.array(self.query_cost, dtype=float)
        successful_cost = cost[success]
        n_total = len(success)
        n_success = int(success.sum())
        total_control = float(sum(self.control_cost_per_tick))

        recov = [r for r in self.recovery_times() if r is not None]
        recov_none = sum(1 for r in self.recovery_times() if r is None)

        return {
            "n_queries": n_total,
            "success_rate": n_success / n_total if n_total else float("nan"),
            "throughput_per_tick": n_success / self.ticks if self.ticks else float("nan"),
            "mean_cost": float(np.mean(successful_cost)) if n_success else float("nan"),
            "p90_cost": float(np.percentile(successful_cost, 90)) if n_success else float("nan"),
            "total_control_cost": total_control,
            "control_overhead_ratio": total_control / (total_control + n_success) if (total_control + n_success) else float("nan"),
            "n_disruptions": len(self.disruption_ticks),
            "mean_recovery_ticks": float(np.mean(recov)) if recov else float("nan"),
            "n_never_recovered": recov_none,
        }
