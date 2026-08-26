# AntSQL: Decentralized Stigmergic Routing for Federated SQL Under Churn

## Abstract

Federated SQL deployments at the edge and across organizational boundaries
cannot always rely on a reachable coordinator with a current global topology.
AntSQL explores a decentralized routing layer in which local gateways maintain
reinforced, evaporating route preferences toward partition-owning shards. The
system is designed to route SQL requests over existing durable databases rather
than replace storage engines. A preliminary discrete-event simulator compares
AntSQL with static centralized routing, periodically adaptive centralized
routing, and decentralized greedy routing. In a 14-node simulation, AntSQL
does not match centralized tail latency under stable conditions. At severe
compound churn, however, it completes more requests than the adaptive
centralized baseline (0.690 versus 0.457 success rate at churn 0.20). These
results are preliminary: they require larger seed counts, per-failure-mode
ablations, and validation with a PostgreSQL-backed gateway before supporting a
systems claim.

## 1. Introduction

Distributed SQL systems typically assume a control plane that can discover
current placement, compute a plan, and distribute routing decisions. That
assumption weakens in edge and federated deployments where sites are
intermittently reachable, membership changes, and organizational boundaries
limit global visibility. AntSQL studies whether persistent local reinforcement
can retain useful routing knowledge long enough to improve availability during
coordination-disrupting churn.

AntSQL is not proposed as a universally faster optimizer. The hypothesis is
conditional: decentralized stigmergic routing may trade steady-state path
quality for better completion and recovery once topology change outpaces a
coordinator's observation and dissemination loop.

## 2. Background and positioning

The relevant prior work is adaptive query processing, not only ant-colony
optimization. Eddies and SkinnerDB establish adaptive execution; SwAP and
Bonfils & Bonnet demonstrate decentralized adaptive query/operator decisions.
AntNet demonstrates persistent reinforced routing under changing networks.
AntSQL's proposed intersection is decentralized query routing with persistent
pheromone-like credit assignment, evaluated against an adaptive centralized
baseline under compound churn. The full positioning and reading notes are in
`research/related_work.md`.

## 3. Design

Each AntSQL gateway exposes SQL to a client, identifies a distribution-key
partition, and routes a request to the owning PostgreSQL shard through local
neighbors. A routing table is keyed by `(table, partition, neighbor)`.
Successful requests reinforce the chosen next hops proportionally to observed
end-to-end cost; stale trails evaporate; failures penalize the final failed
next hop. Requests carry a hop budget and visited-node set to prevent loops.

The production design uses PostgreSQL for persistence and single-shard
transactions, Apache Arrow Flight SQL for client access, and internal gRPC for
gateway forwarding. Version one supports single-shard reads/writes selected by
an exact distribution-key predicate. Multi-shard aggregates and distributed
transactions are intentionally out of scope until their correctness semantics
are implemented and tested.

## 4. Evaluation methodology

We compare four conditions:

1. Static centralized routing, recomputed only at startup.
2. Adaptive centralized routing, periodically recomputed and broadcast.
3. Decentralized greedy routing without persistent pheromone trails.
4. AntSQL's decentralized reinforced routing.

The simulator injects node death/revival, shard migration, link latency shifts,
and workload shifts. It records success rate, throughput, p90 path cost,
control overhead, recovery time, and non-recovery count. The key hypothesis is
a crossover churn rate, lambda-star, where decentralized completion becomes
better than centralized coordination.

## 5. Preliminary results

The current resilience study uses 14 nodes, 10 shards, 400 ticks, and three
deterministic seeds per condition. It is evidence for further work, not a
statistically sufficient conclusion.

| Churn rate | Adaptive centralized success | AntSQL success |
| ---: | ---: | ---: |
| 0.00 | 1.000 | 0.987 |
| 0.05 | 0.823 | 0.746 |
| 0.10 | 0.790 | 0.635 |
| 0.20 | 0.457 | 0.690 |

At churn 0.20, AntSQL throughput is 4.742 successful queries/tick versus
3.176 for adaptive centralized routing. It also has worse p90 path cost
(67.077 versus 36.722) and higher control overhead (0.297 versus 0.179).
Thus the observed advantage is availability under severe churn, not latency or
universal performance.

## 6. Limitations

The simulator abstracts SQL execution as shard reachability and path cost. It
does not implement distributed joins, transactions, real wire protocols, or
production failure detection. A 1,000-node flat simulation also failed to
scale: uniform discovery yielded approximately 1% success and near-total
control overhead. The production architecture therefore requires a hierarchical
gateway overlay rather than every node learning every shard route.

## 7. Next steps

The immediate engineering milestone is a PostgreSQL-backed C++ gateway with
Arrow Flight SQL, real SQL parsing, and native multi-process fault tests. The
immediate research milestone is a 20+ seed study that separates individual
churn arms and measures whether lambda-star scales with coordinator latency and
network diameter.

## References

- A. Avnur and J. Hellerstein. *Eddies: Continuously Adaptive Query Processing*. SIGMOD, 2000.
- I. Trummer et al. *SkinnerDB: Regret-Bounded Query Evaluation via Reinforcement Learning*. SIGMOD, 2019.
- G. Di Caro and M. Dorigo. *AntNet: Distributed Stigmergetic Control for Communications Networks*. JAIR, 1998.
- G. Zhou et al. *Scalable Distributed Stream Processing*. VLDB, 2003. [Verify exact SwAP bibliographic details before submission.]
- N. Bonfils and P. Bonnet. *Adaptive and Decentralized Operator Placement for In-Network Query Processing*. IPSN, 2003. [Verify exact title/details before submission.]
