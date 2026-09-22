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
paired seeds per churn rate on a 14-node topology — every strategy run
against the identical topology, shard placement, workload, and churn
schedule at a given trial, not an independently-drawn one — AntSQL beats
the decentralized greedy ablation at every non-zero churn rate tested (e.g.
0.566 vs. 0.378 success rate at churn 0.20), isolating persistent
stigmergic credit assignment as the source of its advantage rather than
decentralization alone. Against the adaptive-centralized baseline, AntSQL
ties at low churn (0.00-0.05) and shows a statistically resolved,
**monotonically growing** advantage from churn 0.10 through the most severe
rate tested (0.30: +0.053 success-rate points, 95% CI [0.039, 0.068]) — not
the bounded crossover window this paper previously reported. That earlier
finding (a reversal in AntSQL's favor at intermediate churn that flipped
back against it at churn 0.30) came from a 20-seed study whose seeding let
every strategy draw an independent environment even "at the same trial";
once seeds are paired so all strategies share one environment per trial,
the reversal does not replicate, and a near-perfect correlation
(r ≈ 0.99) between AntSQL's and adaptive-centralized's paired outcomes
confirms most of the original study's apparent high-churn variance was
uncontrolled environmental noise, not strategy-specific behavior. A
per-failure-mode ablation and a hop-budget sweep, both undertaken to
explain the now-unreplicated reversal, still surfaced two findings that
stand on their own: adaptation (centralized or stigmergic) specifically
helps under shard migration, and AntSQL's success rate is disproportionately
sensitive to a tight routing hop budget, likely because its local,
exploration-driven walk needs more hops on average than a centrally-computed
shortest path. These results revise, for the second time, an earlier
3-seed preliminary result — landing closer to that result's original
monotonic-advantage shape, but via a corrected methodology rather than a
lucky small sample. They remain preliminary: the failure-mode ablation is
still unpaired, per-environment variance is high even with pairing
controlling its cross-strategy component, the simulator abstracts SQL
execution as shard reachability and path cost, and no PostgreSQL-backed
gateway validation exists yet.

## 1. Introduction

Distributed SQL systems typically assume a control plane that can discover
current placement, compute a plan, and distribute routing decisions. That
assumption weakens in edge and federated deployments where sites are
intermittently reachable, membership changes, and organizational boundaries
limit global visibility. AntSQL studies whether persistent local reinforcement
can retain useful routing knowledge long enough to improve availability during
coordination-disrupting churn.

AntSQL is not proposed as a universally faster router. The hypothesis is
conditional: decentralized stigmergic routing trades steady-state path
quality for a completion advantage over centralized coordination that
holds at moderate-to-severe churn but not on a stable network, where
AntSQL and adaptive-centralized routing are statistically indistinguishable
(§5). An earlier analysis of this same simulator reported that advantage as
a *bounded* churn band that reversed at the most severe rate tested; §6
explains why that reversal turned out to be an artifact of an unpaired
experimental design rather than a real property of the system. It also is
not proposed as evidence that decentralization alone helps: an ablation
without persistent reinforcement is required to separate that claim from
the specific mechanism AntSQL contributes.

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
paired seeds per churn rate, and five churn rates. "Paired" means the seed
for a given (churn rate, trial) does not depend on which strategy is being
run: all four strategies at trial *i* execute against the identical
topology, shard placement, workload arrival stream, and churn schedule,
differing only in routing strategy. This supersedes an unpaired 20-seed
study (`results/resilience_study_v2.csv`), which itself superseded an
earlier 3-seed study; §6 covers why the unpaired study's headline finding —
a reversal at the highest churn rate — does not survive pairing, and is
the more interesting methodological story. Raw data:
`results/resilience_study_v3_paired.csv`; summary:
`results/resilience_study_v3_paired_summary.csv`.

| Churn | Static central. | Adaptive central. | Decentralized greedy | AntSQL |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 | 1.000 | 1.000 | 0.989 | 0.994 |
| 0.05 | 0.701 | 0.858 | 0.731 | 0.856 |
| 0.10 | 0.543 | 0.772 | 0.592 | **0.783** |
| 0.20 | 0.333 | 0.531 | 0.378 | **0.566** |
| 0.30 | 0.305 | 0.542 | 0.351 | **0.596** |

Success rate, mean across n = 20 paired trials. Bold marks churn rates
where AntSQL's paired advantage over adaptive-centralized is statistically
resolved (95% CI on the mean paired difference excludes zero; see below).

| Churn | AntSQL − adaptive (paired) | 95% CI | AntSQL − greedy (paired) | 95% CI |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 | -0.007 | [-0.013, +0.000] | +0.005 | [-0.002, +0.012] |
| 0.05 | -0.003 | [-0.013, +0.008] | +0.125 | [+0.098, +0.153] |
| 0.10 | +0.012 | [+0.001, +0.023] | +0.191 | [+0.163, +0.220] |
| 0.20 | +0.035 | [+0.022, +0.049] | +0.188 | [+0.150, +0.226] |
| 0.30 | +0.053 | [+0.039, +0.068] | +0.244 | [+0.197, +0.292] |

Three findings follow:

- **The greedy ablation is decisively beaten at every non-zero churn
  rate, more clearly than before.** The paired gap (0.125-0.244) is larger
  and every interval excludes zero at churn ≥ 0.05. Since both conditions
  are equally decentralized and differ only in whether routing decisions
  carry a persistent, evaporating trail, this isolates stigmergic credit
  assignment specifically as the source of AntSQL's advantage, not
  decentralization in general.
- **Against adaptive-centralized routing, AntSQL ties at low churn and
  shows a growing, statistically resolved advantage from churn 0.10
  onward — not a bounded crossover window.** The gap is not significant at
  churn 0.00-0.05 (both intervals include zero), then becomes positive and
  significant at 0.10, 0.20, and 0.30, increasing monotonically with churn
  (+0.012 → +0.035 → +0.053). There is no reversal at the highest churn
  rate tested. §6 explains why an earlier unpaired run of this same
  comparison found the opposite at churn 0.30.
- **AntSQL's advantage, where it exists, is still not a latency or
  overhead advantage.** At every churn rate, AntSQL has worse p90 path cost
  (e.g. 99.8 vs. 49.0 at churn 0.30) and higher control overhead (0.294 vs.
  0.168) than adaptive-centralized routing. The claim under test is
  availability/completion during coordination disruption, not universal
  performance; this part of the picture is unchanged by pairing.

## 6. Three attempts at this result, and why the third one is trusted

This comparison has now been run three ways, and each rerun changed the
conclusion. That is reported here as a finding in its own right, not
buried as revision history, because the reason the second attempt was
wrong is a general lesson about simulation methodology, not a fact specific
to AntSQL.

**Attempt 1 (3 seeds, churn 0.00/0.05/0.10/0.20 only).** Reported AntSQL
beating adaptive-centralized routing at churn 0.20 by a wide margin (0.690
vs. 0.457) — a monotonic advantage growing with churn, so far as the swept
range showed.

**Attempt 2 (20 independent seeds, churn 0.00-0.30, `resilience_study_v2.csv`,
formerly this section's headline result).** At churn 0.20 it found a
smaller but still-real gap (0.665 vs. 0.533) — consistent in direction with
attempt 1, but with a much narrower margin and a large standard deviation
on the adaptive-centralized side (± 0.223). Extending to churn 0.30, absent
from attempt 1, reversed the comparison entirely (0.377 vs. 0.514): AntSQL
behind, not ahead. This was reported as a genuine crossover window — AntSQL
competitive only at intermediate churn — and motivated the per-failure-mode
ablation and hop-budget sweep in §7 to explain the reversal.

**Attempt 3 (20 paired seeds, churn 0.00-0.30, §5's table, current
headline result).** `experiment.run_sweep`, which produced attempt 2,
derives each trial's seed from a hash that includes `strategy_name`. Every
strategy therefore drew an independently-generated topology, shard
placement, workload, and churn schedule even "at the same trial" — none of
attempt 2's strategy-vs-strategy comparisons were paired, so all of the
environmental variance documented in attempt 2 (large: std comparable to
or larger than the gap between conditions at churn ≥ 0.20) landed
undivided in every gap estimate instead of partly canceling. Attempt 3
reran the identical configuration with the seed depending only on
`(churn_rate, trial)`, so all four strategies at a given trial share one
environment (`run_resilience_study_paired.py`). The result reverses
attempt 2's reversal: AntSQL statistically ties adaptive-centralized at
churn 0.00-0.05 and is significantly ahead at 0.10, 0.20, and 0.30, with no
sign flip anywhere in the swept range (§5).

Two pieces of evidence support trusting attempt 3 over attempt 2, not just
preferring it because it is newer:

- **Pairing visibly did what it was supposed to.** At churn 0.30, AntSQL's
  and adaptive-centralized's paired success rates across the 20 shared
  environments correlate at r ≈ 0.99 — nearly all of each strategy's
  run-to-run variance is a *shared* environment effect (some churn
  schedules are hard for every strategy, some are easy for all of them),
  not strategy-specific noise. Pairing is designed to cancel exactly this
  component out of the difference, and the resulting standard errors on
  the paired gap (0.007-0.014) are roughly an order of magnitude tighter
  than attempt 2's per-strategy standard deviations (0.10-0.23), consistent
  with that cancellation actually happening rather than being assumed.
- **The two studies' raw distributions at churn 0.30 look like different
  populations, and only one design explains why that's expected.** Attempt
  2's 20 AntSQL success-rate draws at churn 0.30 range from 0.096 to 0.861
  (mean 0.377, skewed low); attempt 3's 20 draws range from 0.159 to 0.857
  (mean 0.596, skewed high). Both are legitimate samples from a
  high-variance, possibly heavy-tailed distribution over random
  environments — nothing in the code changed between them (verified against
  git history for `sim/antsql_sim/{simulator,churn,topology,workload,shards}.py`
  and the routing strategies), and every random draw in the simulator uses
  an explicit, per-instance `numpy.random.default_rng(seed)`, not global
  RNG state, so there is no ordering or cross-run contamination to explain
  the gap either. Two *independent* 20-environment samples landing this far
  apart is exactly what unpaired sampling of a high-variance quantity can
  do, and it is indistinguishable, from inside attempt 2 alone, from a real
  effect — which is the whole problem pairing exists to solve: it does not
  reduce the true variance of any one strategy's outcome, it correlates the
  two strategies' draws so the shared component of that variance cancels in
  the comparison that actually matters.

Neither attempt 1 nor attempt 2 was wrong as a measurement of what it
measured. Attempt 1 simply didn't have enough seeds or a wide enough churn
sweep to see the shape of the relationship; attempt 2 had enough seeds to
look authoritative but an experimental design that let the dominant source
of variance (the environment) leak into the comparison unpaired. The
practical lesson, consistent with §8's discussion of the failure-mode
ablation: at this node count and churn range, per-seed environmental
variance is large enough that a strategy-vs-strategy conclusion drawn from
independent samples — however many seeds — should be treated with real
suspicion until it has been checked under a paired design.

## 7. Per-failure-mode ablation

*Framing note: this section (and the hop-budget sweep at its end) was
undertaken to explain a churn-0.30 reversal against adaptive-centralized
routing that §6 has since traced to an unpaired experimental design and
found does not replicate under a paired one — so the specific phenomenon
motivating the question below may never have been real. The ablation's own
findings (below) do not depend on the reversal being real and are reported
as-is; the hop-budget sweep's finding, resolved only after being rerun
paired, stands independently as a real, separate effect (its closing
discussion explains how it relates to §6).*

The (as it turns out, unpaired) attempt-2 study in §6 leaves an open
question: which churn arm drives the reversal at churn 0.30? We reran each
of the five churn arms — node death, node
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
rather than answers, the open question from section 6: it is no longer
"which arm" but whether AntSQL's hop budget and multi-hop path length
specifically explain the compound-churn gap. Raw data:
`results/failure_mode_ablation.csv`.

**Update — the hop-budget test was run, and came back inconclusive at 20
independent seeds.** We swept `max_query_hops` (the routing walk's hop
budget, shared by every strategy) over {4, 6, 8, 10, 15, 25} at churn 0.20
and 0.30, 20 seeds, comparing only adaptive-centralized and AntSQL. The
AntSQL-minus-adaptive success-rate gap did not move monotonically with hop
budget in either direction (churn 0.30: -0.06 at hop budget 4, +0.10 at
hop budget 10-15, back to -0.03 at hop budget 25) — a pattern that would
support the multi-hop-fragility hypothesis would show the gap moving in
one direction as the budget shrinks or grows, not oscillating. Per-cell
standard deviations (0.10-0.20) are larger than every cross-hop-budget
difference in the gap, so this specific hypothesis was not confirmed at 20
independent seeds. Raw data: `results/hop_budget_sweep.csv`.

**Update 2 — a paired rerun resolves it, in the opposite direction than
hypothesized, and reopens section 6.** The independent-seed sweep above
draws each strategy's topology, shard placement, workload, and churn
schedule from a seed that also depends on `strategy_name`, so "the same
trial" for adaptive-centralized and AntSQL was actually two different
environments — none of it was paired, which is exactly the extra noise
source the per-cell standard deviations above are consistent with.
`run_resilience_study_paired.py`'s sibling script reran the identical
sweep with the seed depending only on `(churn_rate, trial)`, so both
strategies at a given trial share one environment and the gap becomes a
matched-pairs difference instead of a difference of independent means:

| Churn | Hop budget | AntSQL − adaptive (paired) | 95% CI | Excludes 0? |
| ---: | ---: | ---: | ---: | :---: |
| 0.20 | 4 | -0.104 | [-0.129, -0.079] | yes |
| 0.20 | 6 | -0.046 | [-0.068, -0.023] | yes |
| 0.20 | 8 | +0.004 | [-0.013, +0.021] | no |
| 0.20 | 10 | +0.019 | [+0.002, +0.037] | yes |
| 0.20 | 15 | +0.029 | [+0.012, +0.047] | yes |
| 0.20 | 25 | +0.029 | [+0.012, +0.047] | yes |
| 0.30 | 4 | -0.069 | [-0.094, -0.045] | yes |
| 0.30 | 6 | -0.026 | [-0.045, -0.008] | yes |
| 0.30 | 8 | +0.011 | [-0.003, +0.025] | no |
| 0.30 | 10 | +0.029 | [+0.015, +0.043] | yes |
| 0.30 | 15 | +0.039 | [+0.025, +0.053] | yes |
| 0.30 | 25 | +0.039 | [+0.025, +0.053] | yes |

n = 20 paired trials per cell; CI is a normal-approximation 95% interval on
the mean paired difference, not an independent-samples interval. Raw data:
`results/hop_budget_sweep_paired.csv`; summary:
`results/hop_budget_sweep_paired_summary.csv`. hop budget 15 and 25 are
bit-for-bit identical per trial — expected, since a loop-free walk on a
14-node topology cannot exceed 13 hops, so both budgets are already
"effectively unlimited."

Two things follow. First, pairing worked: standard errors here (0.007-0.013)
are roughly an order of magnitude tighter than the independent-seed run's
per-cell standard deviations, and the result is now a clean, monotonic,
mostly-significant curve rather than an oscillation — confirming section
6's suspicion that the noise floor, not the absence of an effect, was
hiding it. Second, the effect that was hiding runs the **opposite**
direction from the multi-hop-fragility hypothesis in section 7's opening
paragraph: that hypothesis predicted a *tight* hop budget should help
AntSQL (fewer simultaneously-alive hops required, so more likely to fit
inside the budget) and a *generous* one should hurt it. The data says the
reverse — AntSQL is significantly *worse* than adaptive-centralized at hop
budgets 4 and 6, crosses to no-significant-difference at 8, and is
significantly *better* from 10 upward, plateauing once the budget stops
constraining anything. The likely mechanism is budget starvation, not
path-length fragility: AntSQL's local, exploration-driven walk has no
global shortest-path knowledge and can wander before finding the owning
shard, so a tight `max_query_hops` disproportionately cuts off its
attempts as `RouteExhausted` before a centrally-computed route (which
already knows the direct path) would run out; once the budget is generous
enough not to bind, that penalty disappears and AntSQL's adaptivity
advantage (section 7's shard-migration finding) can show through instead.

This result is what first cast doubt on attempt 2's churn-0.30 reversal
(§6). The main resilience study's `max_query_hops` is 15 — exactly the
value at which the row above shows AntSQL *ahead* of adaptive-centralized
at both churn 0.20 (+0.029) and, notably, churn 0.30 (+0.039 — the churn
rate where the (now-superseded) attempt-2 study reported AntSQL *behind*
by -0.137, the finding that motivated this entire investigation). This
sweep and the attempt-2 study are not the same seeds and are not directly
interchangeable, but a same-hop-budget paired comparison landing on the
opposite sign from the unpaired main study — using the same
variance-reduction technique that had just fixed this section's own
oscillating result — was reason enough to distrust the unpaired
churn-0.30 finding rather than the paired one, and to rerun the main
resilience study itself with paired seeding rather than infer the answer
by analogy. §6 (attempt 3) reports that rerun: it confirms AntSQL ahead of
adaptive-centralized at churn 0.30, with no reversal anywhere in the swept
range.

## 8. Limitations

The simulator abstracts SQL execution as shard reachability and path cost.
It does not implement distributed joins, transactions, real wire protocols,
or production failure detection. A 1,000-node flat simulation also failed to
scale: uniform discovery yielded approximately 1% success and near-total
control overhead. The production architecture therefore requires a
hierarchical gateway overlay rather than every node learning every shard
route. H1's predicted scaling of lambda-star with coordinator round-trip
time and network diameter (§4) has not been tested — the current study
fixes both.

The per-failure-mode ablation (§7) remains unpaired: `run_failure_mode_ablation.py`
derives each trial's seed from a hash that includes `strategy_name`, so
(unlike §5's main resilience study and §7's hop-budget sweep, both now
paired) every strategy in that table drew an independently-generated
topology, shard placement, workload, and churn schedule even "at the same
trial." §6 found that this exact gap was responsible for a reversal that
did not survive pairing in the main study, so the failure-mode ablation's
own numbers — reported in §7 largely to answer a question (which arm
drives the churn-0.30 reversal) that §6 later found may not have had a
real answer to begin with — should be treated with the same suspicion
until rerun paired; its qualitative findings that generalize regardless
(latency/workload shift move nothing; shard migration is where adaptation
specifically matters) are more likely to survive that rerun than any
specific success-rate margin in its table. It also isolates single arms
cleanly but cannot rule out interaction effects among them; a factorial
design (pairs and triples of arms run together) would be needed for that,
independent of the pairing fix.

Even with cross-strategy pairing, per-environment variance is still large
(§6): pairing cancels the *shared* component of that variance out of a
strategy-vs-strategy comparison, but does not shrink any single strategy's
own run-to-run variance, which is what a claim about one strategy's
absolute success rate (rather than a gap between two strategies) would
need to control. The paired resilience study's standard errors on the
*gap* (§5) are tight (0.007-0.020), but that is a statement about the
comparison, not about how tightly any one strategy's own performance is
pinned down at a given churn rate.

## 9. Next steps

Items (a) and (c) from the previous version of this section — get a
variance-reduction design in place, and resolve the hop-budget hypothesis
under it — are done (§5-7); doing them reopened the question they were
meant to help answer, so the immediate research milestones have changed:

- **Rerun the per-failure-mode ablation (§7) paired.** It is the one table
  in this paper still using the unpaired seeding that §6 showed produces
  spurious reversals; until it's rerun with `(churn_rate, arm, trial)`-only
  seeding, its specific success-rate margins (as opposed to its qualitative
  latency/workload/shard-migration findings, which do not obviously depend
  on pairing) should be treated as provisional.
- **Fold paired seeding into `experiment.run_sweep` itself**, rather than
  leaving it as a property of two one-off scripts
  (`run_hop_budget_sweep.py`, `run_resilience_study_paired.py`). The
  unpaired path should probably require an explicit opt-in flag going
  forward, not be the default, given what it produced here.
- **Extend the churn sweep past 0.30** now that the paired result shows a
  *monotonically growing* AntSQL advantage rather than a bounded window —
  does the advantage keep growing, plateau, or itself eventually reverse at
  more extreme churn than tested here? The old crossover-window framing
  made this question moot; the new monotonic one does not.
- **Attribute the growing paired advantage to a specific mechanism**,
  rather than resting on the completion-rate numbers alone. §4's H1
  hypothesis (a coordinator's gather-recompute-redistribute round trip has
  a fixed latency cost churn can outpace) is consistent with the paired
  result but untested directly; `mean_recovery_ticks` and
  `n_never_recovered`, already recorded per trial, are the natural next
  metrics to check against it.
- **Sweep network diameter and simulated coordinator round-trip time** to
  test H1's specific prediction that lambda-star moves in the predicted
  direction, not just that an advantage exists somewhere (unchanged from
  the previous version of this list).

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
