"""One-off diagnostic (not part of the experiment harness): does antsql's
p90 cost trend down over a longer horizon, or is it stuck? Bins successful
query cost into 50-tick windows and prints the trend.
"""
import numpy as np

from antsql_sim.churn import ChurnScheduler
from antsql_sim.routing import STRATEGIES
from antsql_sim.shards import ShardMap
from antsql_sim.simulator import Simulator
from antsql_sim.topology import generate_topology
from antsql_sim.workload import WorkloadGenerator

n_nodes, n_shards, ticks = 14, 10, 1000
seed = 42

graph = generate_topology("nsfnet_like", n=n_nodes, seed=seed)
node_ids = list(graph.nodes())
shard_map = ShardMap(n_shards=n_shards, node_ids=node_ids, seed=seed + 1)
workload = WorkloadGenerator(node_ids=node_ids, n_shards=n_shards, arrival_rate_per_node=0.5, seed=seed + 2)
churn = ChurnScheduler(graph=graph, n_shards=n_shards, churn_rate=0.0, seed=seed + 3)
strategy = STRATEGIES["antsql"](graph, shard_map, node_ids, seed=seed + 4)

sim = Simulator(graph=graph, shard_map=shard_map, workload=workload, churn=churn, strategy=strategy)
metrics = sim.run(ticks)

ticks_arr = np.array(metrics.query_ticks)
success_arr = np.array(metrics.query_success)
cost_arr = np.array(metrics.query_cost)

window = 50
print(f"{'window':>12} {'success_rate':>13} {'mean_cost':>10} {'p90_cost':>10} {'n_success':>10}")
for lo in range(0, ticks, window):
    hi = lo + window
    mask = (ticks_arr >= lo) & (ticks_arr < hi)
    if mask.sum() == 0:
        continue
    s = success_arr[mask]
    c = cost_arr[mask][s]
    print(
        f"{lo:>5}-{hi:<5} {s.mean():>13.3f} "
        f"{(c.mean() if len(c) else float('nan')):>10.2f} "
        f"{(np.percentile(c, 90) if len(c) else float('nan')):>10.2f} {len(c):>10}"
    )

# average pheromone concentration as a convergence proxy: for each (node,
# shard) with entries, what fraction of weight sits on the single best
# neighbor? 1/degree = uninformed uniform; closer to 1.0 = converged.
concentrations = []
for node, per_shard in strategy.pheromone.items():
    for shard_id, neigh_map in per_shard.items():
        if len(neigh_map) >= 2:
            vals = np.array(list(neigh_map.values()))
            concentrations.append(vals.max() / vals.sum())
print(f"\nmean pheromone concentration on best neighbor: {np.mean(concentrations):.3f} (n={len(concentrations)} table entries)")
print(f"control_overhead_ratio (full run): {metrics.summary()['control_overhead_ratio']:.3f}")
