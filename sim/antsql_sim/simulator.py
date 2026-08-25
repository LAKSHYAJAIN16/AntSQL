"""Ties topology + shards + workload + churn + a routing strategy together
into a discrete-time tick loop. One call to run() is one trial.
"""
from __future__ import annotations

from antsql_sim.metrics import MetricsCollector
from antsql_sim.topology import alive_neighbors, is_alive


class Simulator:
    def __init__(self, graph, shard_map, workload, churn, strategy, max_query_hops: int = 15):
        self.graph = graph
        self.shard_map = shard_map
        self.workload = workload
        self.churn = churn
        self.strategy = strategy
        self.max_query_hops = max_query_hops

    def run(self, ticks: int) -> MetricsCollector:
        metrics = MetricsCollector(ticks=ticks)
        for tick in range(ticks):
            events = self.churn.step(tick)
            for ev in events:
                if ev.kind == "shard_migration":
                    self.shard_map.migrate(ev.detail["shard"], ev.detail["new_owner"])
                elif ev.kind == "workload_shift":
                    self.workload.shift_hot_shards(ev.detail["new_hot_shards"])
                    if hasattr(self.strategy, "hot_shards"):
                        self.strategy.hot_shards = set(ev.detail["new_hot_shards"])
            metrics.record_churn_events(events)

            control_cost = self.strategy.on_tick(tick)
            metrics.record_control_cost(tick, control_cost)

            for q in self.workload.step(tick):
                success, cost, path = self._route(q)
                self.strategy.report_result(path, q.target_shard, success, cost)
                metrics.record_query(tick, success, cost)

        return metrics

    def _route(self, query):
        g = self.graph
        owner = self.shard_map.owner(query.target_shard)
        if not is_alive(g, query.source):
            return False, 0.0, [query.source]
        path = [query.source]
        cost = 0.0
        current = query.source
        if current == owner:
            return True, cost, path
        for _ in range(self.max_query_hops):
            nxt = self.strategy.next_hop(current, query.target_shard, set(path))
            if nxt is None or nxt not in alive_neighbors(g, current):
                return False, cost, path
            cost += g.edges[current, nxt]["latency"]
            path.append(nxt)
            current = nxt
            if current == owner:
                return True, cost, path
        return False, cost, path
