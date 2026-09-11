# AntSQL: Decentralized Stigmergic Routing for Federated SQL Under Churn

## Abstract

Federated SQL deployments at the edge and across organizational boundaries
cannot always rely on a reachable coordinator with a current global topology.
AntSQL explores a decentralized routing layer in which local gateways maintain
reinforced, evaporating route preferences toward partition-owning shards,
inspired by stigmergic trail formation in ant colonies. The system routes SQL
requests over existing durable databases rather than replacing storage
engines. A discrete-event simulator compares AntSQL against three controls:
static centralized routing, periodically adaptive centralized routing, and
decentralized greedy routing without persistent reinforcement. Across 20
seeds per condition on a 14-node topology, AntSQL beats the decentralized
greedy ablation at every churn rate tested (e.g. 0.665 vs. 0.385 success rate
at churn 0.20), isolating persistent stigmergic credit assignment as the
source of its advantage rather than decentralization alone. Against the
adaptive-centralized baseline, AntSQL shows a **crossover window**, not a
monotonic advantage: it loses on stable networks and matches or beats
adaptive-centralized coordination in an intermediate churn band (roughly
0.10-0.20), but loses again at the most severe churn rate tested (0.30),
where the centralized baseline's completion degrades more gracefully than
AntSQL's. A per-failure-mode ablation isolating each churn arm finds that no
single arm reproduces this reversal — adaptive-centralized does not beat
AntSQL on any arm run alone — so it must be an interaction effect rather
than any one disruption type; a follow-up sweep of AntSQL's routing hop
budget, the most obvious candidate mechanism, did not confirm a hop-budget
explanation either, so the specific interaction remains an open question.
These results sharpen, and partly revise, an earlier 3-seed preliminary
result that reported a monotonic AntSQL advantage
at high churn. They remain preliminary: variance is high at severe churn
even at 20 seeds,
the simulator abstracts SQL execution as shard reachability and path cost,
and no PostgreSQL-backed gateway validation exists yet.

## 1. Introduction

Distributed SQL systems typically assume a control plane that can discover
current placement, compute a plan, and distribute routing decisions. That
assumption weakens in edge and federated deployments where sites are
intermittently reachable, membership changes, and organizational boundaries
limit global visibility. AntSQL studies whether persistent local reinforcement
can retain useful routing knowledge long enough to improve availability during
coordination-disrupting churn.

AntSQL is not proposed as a universally faster router. The hypothesis is
conditional and, as the results below show, narrower than originally framed:
decentralized stigmergic routing trades steady-state path quality for
completion advantage over centralized coordination only within a bounded
churn band, not for all churn beyond some threshold. It also is not
proposed as evidence that decentralization alone helps: an ablation without
persistent reinforcement is required to separate that claim from the
specific mechanism AntSQL contributes.

## 2. Background and positioning

The relevant prior work is adaptive query processing, not only ant-colony
optimization. Locating AntSQL correctly requires three independent axes:
whether a system revises its plan after execution starts (static vs.
adaptive), whether adaptation requires a coordinator with a global view
(centralized vs. decentralized), and how a local decision improves over time
if decentralized — one-shot greedy local search with no memory of past
trials, versus reinforcement with a persistent, decaying trace whose credit
reflects a path's long-run quality rather than one hop's instantaneous state
(credit assignment). Eddies (Avnur & Hellerstein, 2000) and SkinnerDB
(Trummer et al., 2019) establish adaptive, centralized execution. SwAP (Zhou,
Ooi, Tan & Tok, 2005) and Bonfils & Bonnet (2003) demonstrate decentralized
adaptive query/operator decisions using greedy local search, without a
persistent stigmergic trail and without an adversarial churn benchmark.
AntNet (Di Caro & Dorigo, 1998) demonstrates persistent reinforced routing
under changing networks, but for packet forwarding, not query execution, and
its original evaluation explicitly disabled node/link failure.

AntSQL's proposed intersection is decentralized query routing with
persistent pheromone-like credit assignment, evaluated against both an
adaptive-centralized baseline and a decentralized-but-non-stigmergic
ablation, under compound churn. The full positioning, the three-axis map of
every system considered, and detailed reading notes are in
`research/related_work.md`.

### 2.1 Why a decentralized-greedy control is required, not optional

A two-condition benchmark — AntSQL versus a static optimizer — would
reproduce a result adaptive query processing already established 25 years
ago: any runtime-adaptive system beats a static one under drift. A
three-condition benchmark that adds only an adaptive-centralized baseline
would show whether decentralization helps at all, but would not show whether
AntSQL's specific mechanism — persistent, evaporating reinforcement — is
doing the work, versus any decentralized adaptation, however simple. The
fourth condition, decentralized greedy routing with no persistent trail, is
the control that isolates this: if AntSQL did not clearly beat it, the
paper's real finding would be "any decentralization helps here," a different
and weaker claim than "stigmergic reinforcement specifically helps." Section
5 reports that AntSQL does beat this control at every churn rate tested.

## 3. Design

Each AntSQL gateway exposes SQL to a client, identifies a distribution-key
partition, and routes a request to the owning PostgreSQL shard through local
neighbors. A routing table is keyed by `(table, partition, neighbor)`.
Successful requests reinforce the chosen next hop proportionally to observed
end-to-end cost; stale trails evaporate; failures penalize the final failed
next hop. Requests carry a hop budget and visited-node set to prevent loops.
Reinforcement is strictly local: a relay gateway learns only about the edge
it chose, never about hops further downstream, so a multi-hop path is
credited incrementally at each gateway rather than by a single global update.

The production design uses PostgreSQL for persistence and single-shard
transactions, Apache Arrow Flight SQL for client access, and internal gRPC
for gateway forwarding. Version one supports single-shard reads/writes
selected by an exact distribution-key predicate. Multi-shard aggregates and
distributed transactions are intentionally out of scope until their
correctness semantics are implemented and tested. The C++23 core (router,
gateway routing contract, SQL validation boundary) is implemented and
tested against an in-process forwarder and mutable in-memory topology that
exercise real multi-gateway routing and failure feedback without requiring
gRPC, libpq, or a live PostgreSQL cluster; see `engine/` and the README for
the current milestone.

## 4. Evaluation methodology

We compare four conditions:

1. **Static centralized** routing, recomputed only at startup.
2. **Adaptive centralized** routing, periodically recomputed and broadcast —
   the load-bearing control per §2.1; beating only the static baseline would
   not be a meaningful result.
3. **Decentralized greedy** routing without persistent pheromone trails — the
   ablation that isolates stigmergic credit assignment from decentralization
   itself.
4. **AntSQL's** decentralized reinforced routing.

The simulator injects node death/revival, shard migration, link latency
shifts, and workload shifts. It records success rate, throughput, p90 path
cost, control overhead, recovery time, and non-recovery count. The central
hypothesis, H1, is a churn rate lambda-star at which decentralized
completion overtakes centralized coordination, because a coordinator's
gather-recompute-redistribute round trip has a fixed latency cost that churn
can outpace, while local reinforcement degrades gracefully via evaporation
instead of going globally stale at once. H1 further predicts that lambda-star
should scale with coordinator round-trip time and network diameter — a
prediction not yet tested here (see §9).

## 5. Results

The current resilience study uses 14 nodes, 10 shards, 400 ticks, 20
deterministic seeds per condition, and five churn rates. This supersedes an
earlier 3-seed, 4-churn-rate study; the two are not fully consistent with
each other at the highest shared churn rate, which is itself informative
about how much the small-sample result was noise (§6).

| Churn | Static central. | Adaptive central. | Decentralized greedy | AntSQL |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 | 1.000 ± 0.000 | 1.000 ± 0.000 | 0.988 ± 0.003 | 0.994 ± 0.008 |
| 0.05 | 0.637 ± 0.136 | 0.826 ± 0.085 | 0.680 ± 0.155 | 0.779 ± 0.131 |
| 0.10 | 0.509 ± 0.177 | 0.660 ± 0.192 | 0.542 ± 0.113 | **0.702 ± 0.168** |
| 0.20 | 0.372 ± 0.096 | 0.533 ± 0.223 | 0.385 ± 0.137 | **0.665 ± 0.146** |
| 0.30 | 0.297 ± 0.108 | 0.514 ± 0.181 | 0.289 ± 0.083 | 0.377 ± 0.225 |

Success rate, mean ± standard deviation across n = 20 seeds. Bold marks
churn rates where AntSQL's mean exceeds the adaptive-centralized mean.

Three findings follow from this table:

- **The greedy ablation is decisively beaten at every churn rate above
  zero.** AntSQL beats decentralized-greedy routing by 0.10-0.28 success-rate
  points at every non-zero churn rate (e.g. 0.702 vs. 0.542 at churn 0.10;
  0.665 vs. 0.385 at churn 0.20; 0.377 vs. 0.289 at churn 0.30). Since both
  conditions are equally decentralized and differ only in whether routing
  decisions carry a persistent, evaporating trail, this isolates stigmergic
  credit assignment specifically as the source of AntSQL's advantage, not
  decentralization in general.
- **The crossover against adaptive-centralized routing is a window, not a
  one-sided advantage at high churn.** AntSQL matches or beats
  adaptive-centralized routing in an intermediate band (churn 0.10-0.20) but
  loses at both the stable end (churn 0.00-0.05) and the most severe churn
  rate tested (0.30, 0.377 vs. 0.514). A centralized coordinator that can
  still complete a full gather-recompute-redistribute cycle occasionally
  during even severe churn appears to recover better than AntSQL's purely
  local, budget-limited hop search once churn is frequent enough to also
  repeatedly disrupt AntSQL's in-flight multi-hop attempts. This is a
  materially different, more cautious claim than the original 3-seed study's
  apparent monotonic AntSQL advantage above churn 0.10 (§6).
- **AntSQL's advantage, where it exists, is not a latency or overhead
  advantage.** At every churn rate, AntSQL has worse p90 path cost and
  higher control overhead than adaptive-centralized routing (full metrics in
  `results/resilience_study_v2_summary.csv` and `results/resilience_study_v2.csv`).
  The claim under test is availability/completion during coordination
  disruption, not universal performance.

## 6. What changed from the preliminary 3-seed study, and why that matters

An earlier study (3 seeds, churn rates 0.00/0.05/0.10/0.20 only) reported
AntSQL beating adaptive-centralized routing at churn 0.20 by a wide margin
(0.690 vs. 0.457). The 20-seed rerun at the same churn rate found a smaller
but still real gap (0.665 vs. 0.533) — consistent in direction, but with a
much narrower margin and a large standard deviation on the adaptive-
centralized side (± 0.223) that the 3-seed study could not have detected.
Extending the sweep to churn 0.30, absent from the original study, reversed
the direction of the comparison entirely (0.377 vs. 0.514). Neither number
in the original table was wrong as a measurement, but three seeds were not
enough to support a claim about the general shape of the crossover, and the
absence of a churn-0.30 arm meant the original study could not have
noticed the reversal at all. This is reported here as a methodological
finding in its own right: at this node count and churn range, per-seed
variance is large enough (std comparable to or larger than the gap between
conditions at churn ≥ 0.20) that conclusions about "which condition wins at
high churn" require either many more seeds or a variance-reduction approach
before they can be treated as more than directional.

## 7. Per-failure-mode ablation

Section 6 leaves an open question: which churn arm drives the reversal at
churn 0.30? We reran each of the five churn arms — node death, node
revival, shard migration, latency shift, workload shift — in isolation
(each non-revival arm paired with node revival at an equal rate, so the
network reaches a dynamic equilibrium instead of monotonically dying, the
same reasoning `ChurnScheduler` applies to its own default arm weights),
at churn rates 0.20 and 0.30, 20 seeds per condition, all four strategies.

| Arm | Churn | Static | Adaptive central. | Decentral. greedy | AntSQL |
| --- | ---: | ---: | ---: | ---: | ---: |
| Latency shift | 0.20 | 1.000 | 1.000 | 0.988 | 0.995 |
| Latency shift | 0.30 | 1.000 | 1.000 | 0.988 | 0.993 |
| Workload shift | 0.20 | 1.000 | 1.000 | 0.988 | 0.996 |
| Workload shift | 0.30 | 1.000 | 1.000 | 0.988 | 0.992 |
| Node death | 0.20 | 0.458 | 0.471 | 0.495 | 0.461 |
| Node death | 0.30 | 0.420 | 0.383 | 0.418 | 0.527 |
| Node revival | 0.20 | 0.436 | 0.445 | 0.457 | 0.499 |
| Node revival | 0.30 | 0.384 | 0.333 | 0.411 | 0.412 |
| Shard migration | 0.20 | 0.410 | 0.918 | 0.483 | 0.929 |
| Shard migration | 0.30 | 0.367 | 0.866 | 0.405 | **0.911** |

Success rate, mean across n = 20 seeds. Bold marks the one isolated-arm
cell where a strategy comparison flips sign relative to compound churn.

Three things follow, and together they answer the section-6 question in a
way that raises a sharper one:

- **Latency and workload shift move nothing.** Every strategy stays at or
  above 0.988 success at both rates: these arms cost path quality (already
  visible in AntSQL's worse p90 in §5), not completion, on their own.
- **Node death/revival degrade all four strategies together, with no
  consistent winner.** All four land in a tight 0.33-0.53 band at both
  churn rates; AntSQL is competitive but not dominant here in isolation.
- **Shard migration alone is where adaptation matters — but AntSQL does not
  lose to adaptive-centralized on this arm.** Both strategies that adapt at
  all (adaptive-centralized and AntSQL) hold 0.87-0.93 while the two that
  do not (static-centralized and decentralized-greedy) drop to 0.37-0.48.
  AntSQL is not behind adaptive-centralized here; if anything it is
  slightly ahead at both churn rates.

**No single isolated arm reproduces the churn-0.30 reversal from section
6.** Since adaptive-centralized does not beat AntSQL on any arm run alone,
the reversal in the compound study must be an interaction effect between
arms — most plausibly, simultaneous node death repeatedly breaking
AntSQL's in-flight multi-hop forwarding attempts (every hop on a path must
be alive at once) in a way that does not equally disrupt a centralized
coordinator's single client-to-shard hop once its map happens to be
current, while shard migration in isolation (which both adaptive
strategies handle equally well) is not itself the driver. This narrows,
rather than answers, next steps item (c) in section 9: the open question
is no longer "which arm" but whether AntSQL's hop budget and multi-hop
path length specifically explain the compound-churn gap. Raw data:
`results/failure_mode_ablation.csv`.

**Update — the hop-budget test was run, and came back inconclusive.** We
swept `max_query_hops` (the routing walk's hop budget, shared by every
strategy) over {4, 6, 8, 10, 15, 25} at churn 0.20 and 0.30, 20 seeds,
comparing only adaptive-centralized and AntSQL. The AntSQL-minus-adaptive
success-rate gap did not move monotonically with hop budget in either
direction (churn 0.30: -0.06 at hop budget 4, +0.10 at hop budget 10-15,
back to -0.03 at hop budget 25) — a pattern that would support the
multi-hop-fragility hypothesis would show the gap moving in one direction
as the budget shrinks or grows, not oscillating. Per-cell standard
deviations (0.10-0.20) are larger than every cross-hop-budget difference in
the gap, so this specific hypothesis is not confirmed at 20 seeds: either
hop budget is not the driver, or the effect is real but smaller than the
noise floor at this seed count. Raw data: `results/hop_budget_sweep.csv`.
The interaction behind the churn-0.30 reversal (section 6) is therefore
still unexplained; ruling out one plausible mechanism is itself useful, but
the next attempt should probably use a variance-reduction design (paired
seeds across hop-budget values, not independent ones) rather than more
independent seeds at the same noise floor.

## 8. Limitations

The simulator abstracts SQL execution as shard reachability and path cost.
It does not implement distributed joins, transactions, real wire protocols,
or production failure detection. A 1,000-node flat simulation also failed to
scale: uniform discovery yielded approximately 1% success and near-total
control overhead. The production architecture therefore requires a
hierarchical gateway overlay rather than every node learning every shard
route. Twenty seeds substantially reduced but did not eliminate the variance
problem described in §6, particularly at churn ≥ 0.20 where standard
deviations are 15-45% of the mean; the crossover-window claim in §5 should be
read as directional pending a larger-seed or variance-reduced follow-up. H1's
predicted scaling of lambda-star with coordinator round-trip time and network
diameter (§4) has not been tested — the current study fixes both. The
per-failure-mode ablation (§7) isolates single arms cleanly but cannot rule
out interaction effects beyond the specific hop-budget hypothesis it
motivates; a factorial design (pairs and triples of arms run together) would
be needed to confirm which interaction, specifically, produces the
churn-0.30 reversal.

## 9. Next steps

The immediate research milestones are: (a) a seed count sufficient to bound
the standard error at the churn rates where the crossover window is
narrowest, likely 50-100 seeds or a paired/blocked variance-reduction design
rather than independent seeds — the hop-budget sweep in §7 is a concrete
example of an effect too small to see at 20 independent seeds; (b) sweeping
network diameter and simulated coordinator round-trip time to test H1's
specific prediction that lambda-star moves in the predicted direction, not
just that a crossover exists somewhere; and (c) since the hop-budget/
path-length hypothesis from §7 did not pan out at 20 seeds, either rerunning
it with a paired/variance-reduced design per (a) or, if it still doesn't
resolve, moving on to a different candidate mechanism for the churn-0.30
interaction (e.g. specifically the timing of node death relative to an
in-flight AntSQL routing decision versus a centralized coordinator's most
recent broadcast, rather than path length per se).

On the engineering side, `engine/` now has a real (if deliberately narrow)
`ISqlParser` implementation, `SimpleSqlParser` — hand-written recursive
descent over single-table SELECT/INSERT/UPDATE/DELETE with at most one
integer-equality distribution-key predicate — plus a dependency-free TCP
transport (`TcpForwarder`/`TcpServer`) that moves the same
`QueryRequest`/`QueryResponse` types over real sockets. Both exist because
libpg_query, gRPC, and Arrow Flight SQL remain uninstallable in the current
development environment (no CMake/vcpkg, non-elevated session); both are
built behind the same `ISqlParser`/`IForwarder` interfaces so a
PostgreSQL-grammar parser and a gRPC/Flight SQL transport can replace them
later without changing `Gateway`, `SqlValidator`, or the routing contract.
The next engineering milestone is a libpq shard executor once PostgreSQL
tooling is available, which — combined with `SimpleSqlParser` and either
forwarder — would let the native harness run against real PostgreSQL shards
end-to-end for the first time.

## References

- A. Avnur and J. Hellerstein. *Eddies: Continuously Adaptive Query
  Processing*. SIGMOD, 2000.
- I. Trummer et al. *SkinnerDB: Regret-Bounded Query Evaluation via
  Reinforcement Learning*. SIGMOD, 2019.
- G. Di Caro and M. Dorigo. *AntNet: Distributed Stigmergetic Control for
  Communications Networks*. Journal of Artificial Intelligence Research, 9,
  317-365, 1998. DOI: 10.1613/jair.530.
- Y. Zhou, B. C. Ooi, K.-L. Tan, and W. H. Tok. *An Adaptable Distributed
  Query Processing Architecture*. Data & Knowledge Engineering, 53(3),
  283-309, 2005. (SwAP.)
- B. Bonfils and P. Bonnet. *Adaptive and Decentralized Operator Placement
  for In-Network Query Processing*. Information Processing in Sensor
  Networks (IPSN 2003), LNCS 2634, 47-62, Springer, 2003. DOI:
  10.1007/3-540-36978-3_4.
