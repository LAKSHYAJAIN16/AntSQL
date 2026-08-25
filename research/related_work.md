# AntSQL — Related Work & Positioning

Status: novelty search complete (2026-08-24), based on ACM/IEEE/arXiv/Google
Scholar coverage across the terminology brief specified: stigmergic query
processing, decentralized query planning, emergent query execution,
autonomous distributed databases, self-organizing query processing, plus ACO
and swarm-intelligence variants.

## Verdict (revised after deeper pass — see §0)

The first pass of this search matched on "ant"/"stigmergy" keywords and
missed the closest actual intellectual neighborhood: the **adaptive query
processing** literature (Eddies, SkinnerDB) and its **distributed/sensor-
network descendants** (SwAP, Bonfils & Bonnet). That neighborhood is
closer to AntSQL's real claim than anything ACO-flavored, and it changes
the required experimental design, not just the citation list — see §0 for
why, and for the corrected hypothesis. Read §0 before reading §1–§4; §1–§4
are still accurate but are no longer the main event.

**Short version of the correction:** the mechanism — "no central planner,
local/decentralized execution-time adaptation" — was already built and
evaluated, twice, in 2003 (SwAP at VLDB, Bonfils & Bonnet at IPSN). What
was *not* done, in any paper found so far: (a) an explicit stigmergic
credit-assignment mechanism (pheromone reinforcement + evaporation, as
opposed to greedy local hill-climbing), and (b) a benchmark that compares
against an **adaptive-but-still-centralized** baseline (Eddies/SkinnerDB-
style) under **compound** topology + workload churn, framed as a crossover
parameterized by churn rate vs. coordination latency. Both gaps are real,
but they are narrower and more specific than the original framing, and the
paper has to be honest about SwAP/Bonfils&Bonnet existing or it will not
survive review.

## 0. The real intellectual map (read this section first)

### 0.1 Why the ACO/stigmergy keyword search wasn't enough

Searching "stigmergic query processing," "ant colony database," etc. finds
only work that self-identifies with the ant metaphor. But the actual
scientific question in the brief — *what happens to query execution when
no node has global knowledge and conditions keep changing* — has been
worked on continuously since 2000 under a completely different name:
**adaptive query processing (AQP)**. That field doesn't use ant language,
so a metaphor-keyword search structurally cannot find it. This is a search
methodology lesson worth keeping: novelty searches for a *mechanism*
dressed in a *metaphor* have to also search for the mechanism undressed.

### 0.2 The three axes that actually matter

Every relevant system sits somewhere on three independent axes. AntSQL's
claim only means something once it's located precisely on all three,
against baselines that are also precisely located — a vague "centralized
vs. decentralized" comparison will not survive review because most of the
obvious "centralized" strawmen (static-stats optimizers) were already beaten
by AQP twenty-five years ago, decentralization aside.

- **Axis 1 — static vs. adaptive:** does the system ever revise its plan
  after execution starts, in response to observed runtime conditions?
- **Axis 2 — centralized vs. decentralized:** does adaptation require a
  single coordinator with (at least eventually) a global view, or can each
  node act on purely local information with no node ever assembling the
  whole picture?
- **Axis 3 — credit-assignment mechanism:** if decentralized, *how* does a
  local decision get better over time — greedy local search /
  hill-climbing on an immediate local signal (no memory of past trials), or
  reinforcement with a persistent, decaying trace (pheromone/eddy-routing-
  table-style) that lets a *path's* long-run quality, not just one hop's
  instantaneous state, shape future decisions?

### 0.3 Placing every relevant system on the map

| System | Static/Adaptive | Central/Decentral | Credit assignment | Failure/churn tested? |
|---|---|---|---|---|
| Classical cost-based optimizer (System R-style) | Static | Central | n/a | No — this is the strawman, already known to lose |
| Eddies (Avnur & Hellerstein, SIGMOD 2000) | Adaptive | Central (one eddy, one node) | Per-tuple routing policy (lottery/reinforcement on tuple, not persistent trail across queries) | No — single query, single node, no network churn |
| SkinnerDB (Trummer et al., SIGMOD 2019) | Adaptive | Central (single node) | Regret-bounded RL over join order, time-sliced within one query | No — single node, no distribution at all |
| SwAP (Zhou et al., VLDB 2003) | Adaptive | **Decentralized** (one eddy per site) | Local per-site routing decisions, some inter-site info exchange | Not found yet — need primary source (§0.5) |
| Bonfils & Bonnet (IPSN 2003) | Adaptive | **Decentralized** (walks neighbors only) | **Greedy local hill-climbing** on operator placement, explicitly reports risk of local minima | Static sensor topology; not an adversarial-churn benchmark |
| PIER (Huebsch et al., SIGMOD/VLDB 2003–05) | Mostly static plan, adaptive *substrate* | Decentralized (DHT) | DHT self-healing (routing-layer correctness under churn, not query-plan-quality learning) | Yes, for peer churn — but for *correctness/availability*, not *plan-quality recovery* |
| Borealis (Abadi et al., CIDR 2005) | Adaptive (load shedding, operator migration) | Distributed but **coordinator-assisted** | Load-variance-driven migration, not stigmergic | Yes — explicit fault-tolerance/failure section (§0.5) |
| AntNet-for-DB-plans (§1 below) | n/a (offline search) | Central (ACO runs on one optimizer node) | Pheromone + evaporation | No |
| AntNet packet routing (§2/§2a) | Adaptive | **Decentralized** | Pheromone + evaporation, genuinely stigmergic | Yes, in follow-on papers (§2), not JAIR98 itself |
| **AntSQL (proposed)** | Adaptive | Decentralized | Pheromone + evaporation | **This is meant to be the contribution** |

Reading the table: the "decentralized" cell is not empty (SwAP, Bonfils &
Bonnet, PIER, Borealis all sit there), and the "stigmergic credit
assignment" cell is not empty either (AntNet routing does). What appears
genuinely empty, as of this pass, is the **intersection**: decentralized,
no-global-topology query *execution* (not just packet routing, not just
sensor-network operator placement) using **persistent reinforced-trail**
credit assignment (not one-shot greedy hill-climbing), evaluated against
an **adaptive-centralized** baseline under **compound, adversarial**
churn. Every existing decentralized DB/sensor system found so far either
uses greedy local search (weaker credit assignment than pheromone trails,
and explicitly prone to local minima per Bonfils & Bonnet's own admission)
or hasn't been stress-tested against destruction the way AntNet vs. OSPF
was in routing.

### 0.4 The confound that must be designed out — this is the load-bearing methodological point

If AntSQL's benchmark baseline is a **static**-statistics centralized
optimizer, a positive result is worthless: AQP already proved 25 years ago
that *any* runtime-adaptive system beats a static one under drift. A
reviewer who has read Deshpande/Ives/Raman's survey will reject the paper
on sight for re-deriving a solved result.

The baseline has to be an **adaptive, centralized** system — conceptually
an Eddies/SkinnerDB-style reoptimizer extended to run across a cluster with
a coordinator that gathers statistics and recomputes/redistributes routing
decisions (this is roughly what SwAP or Borealis already are; one of them,
or a faithful reimplementation of the relevant piece, should probably be
Table 1 of the AntSQL paper's evaluation, not just a citation). Only once
AntSQL is shown to beat *that* baseline — a system that is already adaptive,
just not decentralized — is the decentralization claim actually isolated
and interesting.

This yields the real, falsifiable hypothesis, sharper than the original
brief:

> **H1:** There exists a churn rate λ\* (combined rate of node
> death/shard movement/latency shift/workload shift events per unit time)
> such that for λ < λ\*, an adaptive-centralized query executor
> (coordinator-gathered statistics, periodic global reoptimization) matches
> or beats AntSQL's decentralized stigmergic executor, and for λ > λ\*,
> AntSQL wins — because the centralized system's coordination round-trip
> (gather stats → recompute → redistribute) has a fixed latency cost that
> churn events can outpace, while AntSQL's local reinforcement has no
> broadcast step and degrades gracefully with local evaporation instead of
> going globally stale all at once.
>
> **H1 predicts a testable relationship:** λ\* should scale with the
> coordinator's round-trip time and with network diameter — i.e., the
> crossover should move predictably when you change cluster size or
> coordinator placement, not just appear as an unexplained empirical knee
> in one plot. A benchmark that shows *a* crossover without showing it
> moves the *predicted* way when you vary diameter/RTT is a much weaker
> paper than one that does.

To make H1 falsifiable rather than a foregone conclusion, the design also
needs a **decentralized-but-non-stigmergic ablation** (a Bonfils-&-Bonnet-
style greedy local search with no persistent trail, or a PIER-style
DHT-routed-but-non-learning executor) as a second control. If AntSQL only
beats the static baseline and the adaptive-centralized baseline but *not*
the decentralized-greedy ablation, the paper's real finding is "any
decentralization helps," not "stigmergic reinforcement specifically
helps" — a different, weaker, but still worth-knowing result. Structuring
the experiment now to distinguish these is much cheaper than discovering
post-hoc that the ablation was missing.

### 0.5 What to actually read before finalizing the benchmark design (priority order)

1. **Bonfils & Bonnet, IPSN 2003** — closest existing decentralized,
   no-global-topology, adaptive placement algorithm. Read for: exact
   local-decision rule, how they characterize/avoid local minima, and
   whether their evaluation includes any topology perturbation at all
   (their abstract only claims static-topology near-optimality).
2. **Zhou et al. (SwAP), VLDB 2003** — closest existing decentralized
   *adaptive query execution* (not just placement) system. Read for:
   whether per-site eddies exchange enough info to constitute partial
   global knowledge (if so, AntSQL's "zero global knowledge" claim needs
   to be explicitly contrasted), and whether they test any failure/churn
   scenario at all.
3. **Abadi et al., Borealis fault-tolerance paper (SIGMOD 2005)** — likely
   the strongest existing failure/recovery *benchmark methodology* in this
   whole map (closer to a real distributed system than AntNet's packet
   simulator). Read for: their availability/consistency trade-off framing
   and whether their recovery-time metrics can be adapted directly.
4. **Deshpande, Ives & Raman survey (2007)**, specifically its taxonomy of
   what counts as "adaptive" — needed to write a related-work section that
   correctly cites AQP as a *field*, not just two papers, without having
   to read all ~100+ papers it surveys.
5. Only after 1–3: the AntNet follow-on failure papers from §2 (Dead Ants,
   Improved-AntNet) — these matter for the *metric definitions* (recovery
   time, degradation curves) but are lower priority now than confirming
   whether SwAP/Bonfils&Bonnet already tested churn, since that finding
   would directly determine whether AntSQL's contribution is (a) novel
   mechanism + novel benchmark, or (b) just a novel benchmark applied to
   an existing mechanism family. Those are different papers with different
   framings, and it's worth knowing which one this is before writing more.

## 1. Saturated: ACO as a centralized plan-search metaheuristic

All of these use ants as a search heuristic run *on one optimizer node* to
explore the join-order/plan space — they do not remove global knowledge from
the system, and none run execution-time agents over a live, changing
network.

- Distributed Query Plan Generation using Ant Colony Optimization —
  IGI Global. https://www.igi-global.com/gateway/article/122099
- Dynamic Programming with ACO Metaheuristic for Optimization of Distributed
  Database Queries — Springer chapter.
  https://link.springer.com/chapter/10.1007/978-1-4471-2155-8_13
- Join Query Optimization Using Genetic Ant Colony Optimization Algorithm
  for Distributed Databases (GACO-D).
  https://www.researchgate.net/publication/333169247
- Dynamic Cost Ant Colony Algorithm for Optimize Distributed Database Query
  — Springer chapter.
  https://link.springer.com/chapter/10.1007/978-3-030-44289-7_17
- Dynamic Cost Ant Colony Algorithm to Optimize Query for Distributed
  Database Based on Quantum-Inspired Approach — MDPI Symmetry 13(1):70, 2021.
  https://www.mdpi.com/2073-8994/13/1/70
- Query Optimization in Distributed Database Based on Improved Artificial
  Bee Colony Algorithm — MDPI Applied Sciences 14(2):846, 2024.
  https://www.mdpi.com/2076-3417/14/2/846
- Prime Learning–ACO (PL-ACO) query optimization — IJETT.
  https://ijettjournal.org/archive/ijett-v70i8p216

**How to cite them in the paper:** as evidence that ACO-as-search is a known
tool in this space, immediately followed by the distinction — "these apply
ACO centrally as a plan-search heuristic; AntSQL removes the central
planner entirely and lets execution-time stigmergy across a real, changing
network substitute for planning." That sentence is doing the actual novelty
work; without it the reviewer conflates AntSQL with this pile.

## 2a. AntNet (Di Caro & Dorigo, JAIR 1998) — verified detail, pulled from primary source

Full citation: Di Caro, G., & Dorigo, M. (1998). AntNet: Distributed
Stigmergetic Control for Communications Networks. *Journal of Artificial
Intelligence Research*, 9, 317–365. DOI: 10.1613/jair.530. Free full text:
https://arxiv.org/abs/1105.5449 (arXiv reprint of the original).

**Correction to my earlier secondary-source summary:** this paper does
*not* run a node/link-failure experiment. Section 6.1 states explicitly:
"All the networks are simulated with zero link-fault and node-fault
probabilities." AntNet's *failure*-recovery evidence (the part closest to
AntSQL's "destroy nodes" arm) is in the follow-on literature — "Using Dead
Ants to Improve the Robustness and Adaptability of AntNet," "Helping ants
for adaptive network routing," "Improved-AntNet: ACO Routing Algorithm in
Practice" (all §2 above) — not in the original JAIR paper. Read those before
building the node-death arm of the AntSQL benchmark; don't cite JAIR98 for
failure recovery specifically.

**What JAIR98 does test, and this is the closer analogue to AntSQL's
"change the workload" arm:** a temporary traffic-load shock (TMPHS = fixed,
temporary hot-spot traffic superimposed on light background load). On
NSFNET: at simulated t=400s, four hot-spot nodes turn on for 120s
(background MSIA=3.0s, hot-spot MPIA-HS=0.04); on NTTnet: same protocol,
MSIA=4.0s. They plot throughput and packet delay as 5-second moving-window
averages across the full 1000s run (Figures 12 and 16) to show the
transient spike and recovery back to baseline. This is the right template
for AntSQL's workload-shift arm, but says nothing about topology/shard
disruption.

**Exact metrics defined (§6.3):**
- **Throughput** — correctly delivered bits/sec.
- **Delay distribution** — packet delay in seconds. They explicitly reject
  mean/variance as a summary ("packet delays can be spread over a wide
  range of values... cannot be meaningfully parametrized in terms of mean
  and variance") and instead report either the full empirical CDF or the
  **90th-percentile** delay statistic.
- **Network capacity usage / routing overhead** — ratio of bandwidth
  occupied by routing (control) packets to total available bandwidth,
  reported ×10⁻³ (Table 2, §7.4).
- **"Power"** (Figure 17, used to justify a parameter setting, not a
  headline metric) — normalized ratio of delivered throughput to packet
  delay; used to show a wide robust plateau in the ant-generation-rate
  parameter before overhead dominates.

**Experimental protocol (§6, §7):** 10 trials × 1000 simulated seconds per
condition, results averaged across trials (throughput inter-trial variance
reported as "a few percent" — no formal CI given for throughput; a z≈1.70
(~95% confidence) factor is used only internally in AntNet's own routing
statistical model, not for reporting result significance). First 500s of
each run is a warm-up with **no data traffic** — algorithms build initial
routing tables from ant traffic alone before data traffic starts. Load is
swept by decreasing session inter-arrival time (MSIA) across ~5 discrete
steps per traffic-pattern condition, from low load to near-saturation.

**Topologies (mean shortest-path hops μ, variance σ, node count N):**
- *SimpleNet* (μ=1.9, σ=0.7, N=8) — 8 nodes, 9 bidirectional links,
  10 Mbit/s, 1 ms propagation delay. Built specifically to force multi-path
  load-splitting (traffic from node 1→6 exceeds single-link capacity).
- *NSFNET* (μ=2.2, σ=0.8, N=14) — the 1987 USA T1 backbone, 14 nodes,
  21 bidirectional links, 1.5 Mbit/s, 4–20 ms propagation delay.
- *NTTnet* (μ=6.5, σ=3.8, N=57) — NTT's Japanese fiber-optic corporate
  backbone, 57 nodes, 162 bidirectional links, 6 Mbit/s, 1–5 ms delay;
  explicitly noted as *not* well-balanced (unlike NSFNET) — this is where
  AntNet's margin over competitors is largest ("one order of magnitude").

**Traffic model (§6.2):** temporal × spatial, crossed. Temporal: Poisson
(P), Fixed (F, one-to-all sessions set once at t=0), Temporary/hot-spot
(TMPHS). Spatial: Uniform (U), Random (R), Hot Spots (HS). Bit-stream
shape: Constant Bit Rate (CBR) or Generic Variable Bit Rate (GVBR,
negative-exponential packet size and inter-arrival time; default for most
experiments). Mean packet size fixed at 4096 bits across all experiments.

**Baselines compared (§5) and their category:** OSPF (static, link-state —
their simplified IGP implementation, min-time shortest path over a static
512-byte reference packet), SPF (adaptive link-state, ARPANET
hop-normalized-delay metric), BF (adaptive distance-vector, asynchronous
Bellman-Ford), Q-R (Q-Routing, Boyan & Littman 1994 — online Bellman-Ford
via Q-learning), PQ-R (Predictive Q-Routing, Choi & Yeung 1996 — Q-Routing
plus a learned queue-recovery-rate model), Daemon (not a real algorithm —
an oracle upper bound with instantaneous global-queue knowledge and
per-packet-hop shortest-path recomputation; defines the empirical ceiling).

**Headline results to cite precisely (not "beats OSPF" vaguely):**
- SimpleNet (forced multi-path): AntNet throughput approaches Daemon
  closely after a short transient; PQ-R is ~15% below AntNet; all other
  competitors settle ~30% below AntNet.
- NSFNET, near-saturation UP traffic: OSPF/Q-R/PQ-R delays "2 or more
  seconds" (90th percentile); BF/SPF ~50% worse than AntNet, ~65% worse
  than Daemon.
- NTTnet (the unbalanced, larger topology): AntNet vs. every competitor
  differs by "one order of magnitude" in 90th-percentile delay; OSPF
  "completely collapses" under UP-HS load.
- Routing overhead (Table 2, ×10⁻³ of total bandwidth): e.g. NTTnet-UP —
  AntNet 2.85, OSPF 0.14, SPF 3.68, BF 1.39, Q-R 3.72, PQ-R 6.77. **This is
  directly relevant to AntSQL's "global knowledge has a maintenance cost"
  claim** — but note AntNet's own paper frames overhead purely as a
  *bandwidth* cost (and finds it negligible, <1% of capacity across all
  conditions), not as a *staleness* cost. AntSQL's contribution should be
  explicit that the maintenance cost it's pricing is staleness/recovery-time
  under drift, not bandwidth — AntNet's Table 2 doesn't make that argument
  and shouldn't be cited as if it does.
- AntNet's own tunable parameter: ant-generation interval 0.3s/node
  (shown robust across ~3 orders of magnitude in Fig. 17 before overhead
  dominates) — useful precedent for reporting AntSQL's own
  pheromone-evaporation / re-probe-rate sensitivity sweep the same way.

## 2. Closest prior art for the actual thesis: stigmergic routing under churn

This is the domain where "decentralized/stigmergic beats a system with
staling global knowledge, under adversarial topology change" has already
been demonstrated — just for packets, not queries.

- Di Caro & Dorigo, AntNet — the foundational stigmergic routing algorithm;
  probabilistic, load-balances naturally, adapts to topology change without
  global link-state knowledge (vs. OSPF's global-map approach that reacts
  slowly to change).
- "Using Dead Ants to Improve the Robustness and Adaptability of AntNet
  Routing Algorithm." https://www.researchgate.net/publication/263044863
- "Implementation and evaluation of AntNet, a distributed shortest-path
  algorithm" — IEEE. https://ieeexplore.ieee.org/document/1517649/
- "Helping ants for adaptive network routing" — ScienceDirect.
  https://www.sciencedirect.com/science/article/abs/pii/S0016003206000275
- "Improved-AntNet: ACO Routing Algorithm in Practice" — IEEE.
  https://ieeexplore.ieee.org/document/4809732
- Hybrid ant-colony inter-cluster routing for FANETs (flying ad-hoc
  networks) — PMC, 2024. https://www.ncbi.nlm.nih.gov/pmc/articles/PMC11228038/

**Why this matters for the experimental design, not just the citation list:**
the AntNet-vs-OSPF literature already establishes the *shape* of the result
you want (a convergence/recovery-time crossover after a failure event) and
the metrics that made it convincing (time-to-reconverge, path quality during
the transient, degradation under sustained churn vs. one-shot failure). The
adversarial benchmark design (destroy nodes, move shards, alter latencies,
change workload at t=T) should borrow this evaluation protocol directly and
cite it as precedent for *why* this experimental design is the right one to
run — reviewers who know the routing literature will recognize the protocol
and read it as rigor, not novelty theater.

## 3. Baseline-side literature: how conventional optimizers actually go stale

Useful for building the "conventional optimizer" side of the comparison
credibly, and for citing that staleness-under-drift is a recognized,
currently-active problem in DB research (i.e., not a strawman baseline).

- DriftBench — defines/generates data and query workload drift for
  benchmarking. arXiv:2510.10858. https://arxiv.org/pdf/2510.10858
- "Modeling Shifting Workloads for Learned Database Systems" — PACMMOD,
  March 2024.
- Eraser: Eliminating Performance Regression on Learned Query Optimizer —
  PVLDB 17(5), 2024.
- "Conformal Prediction for Verifiable Learned Query Optimization" —
  arXiv:2505.02284, distribution-shift evaluation on CEB/JOBLight-train.
  https://arxiv.org/html/2505.02284

**Use for:** justifying that "global statistics progressively become stale"
is a real, already-measured phenomenon (not asserted for rhetorical
convenience), and possibly reusing DriftBench's drift-generation machinery
rather than inventing a new one — cite it as the mechanism for the
"workload changes at t=T" arm specifically, since it's built for exactly
that.

## 4. Adjacent but distinct: RL/gossip P2P query routing

Not the same claim (these are unstructured-P2P resource discovery, not
distributed-DB query execution over sharded/replicated data with join
semantics), but close enough on "no global knowledge + learn from local
experience" that the paper should distinguish itself from this bucket too.

- Q-learning intelligent neighbor selection for unstructured P2P query
  routing — Applied Intelligence (Springer).
  https://link.springer.com/article/10.1007/s10489-021-02793-6
- Deep Q-Learning based optimal query routing for unstructured P2P —
  TechScience CMC. https://www.techscience.com/cmc/v70n3/45036/html

**Distinction to state explicitly:** these route a query to *a* peer
holding relevant unstructured content; AntSQL must resolve joins/aggregates
across *multiple* shards with correctness constraints (data must actually
be combined correctly), which is a materially harder action space than
next-hop peer selection.

## 5. Positioning paragraph (draft, for the paper's related-work section)

> Prior work applies ant colony optimization to distributed query
> optimization by running ACO as a centralized search heuristic over the
> plan space [cite §1] — the colony is simulated inside a single optimizer
> node, and the system retains a conventional, centrally-computed execution
> plan. Separately, stigmergic routing algorithms such as AntNet [cite §2]
> eliminate the central planner entirely for the (much simpler) problem of
> packet forwarding, and are known to recover faster than link-state
> protocols such as OSPF after topology change, because no node needs an
> up-to-date global map. AntSQL asks whether that same result holds for
> query execution, where the decision space is join order, operator
> placement, and shard/replica selection rather than a single next hop, and
> where staleness in a conventional optimizer's cost model — increasingly
> recognized as a live problem [cite §3] — plays the role OSPF's stale
> link-state database plays in the routing literature.

## 9. Idea space: directions beyond the core crossover experiment

Brainstormed and spot-checked against prior art (2026-08-24, same session
as §0). Each idea below is a genuine fork, not a rephrasing — pick zero,
one, or several; they compose. Novelty check is honest, not optimistic:
where something adjacent already exists, that's stated plainly.

### 9.1 Data migrates to the query, not just the query to the data (Physarum framing)

AntSQL as scoped so far is entirely about **routing queries** through a
fixed data layout — ants find paths to shards that don't move. A distinct
mechanism: let the *data placement itself* be the thing reinforced.
Physarum polycephalum's tube-thickening rule (flux-carrying tubes thicken,
idle ones shrink and disappear — the mechanism behind the famous Tokyo-
rail-network replication result) is a different, well-studied stigmergic
process from ant-trail pheromone, and network-design/Steiner-tree/TSP
applications of it are a real literature (Physarum-inspired Network
Optimization: A Review, arXiv:1712.02910; Physarum solutions to network
optimization, ResearchGate). **What's not found:** anyone applying it to
*shard/replica placement in a distributed database*, where "flux" =
observed query access frequency and "tube" = a replica or shard's
existence at a given node. This is a second, independent stigmergic
process (data-placement pheromone) that could run *alongside* AntSQL's
query-routing pheromone — and comparing "ants find data" vs. "data flows
to ants" vs. both together is a real 3-way experimental question, not a
naming exercise: they have different cost profiles (query routing is
cheap/per-request; data migration is expensive/rare) and different failure
modes under churn.

### 9.2 Adversarial pheromone poisoning — a security paper, not just a systems paper

Ant-colony routing has a known, actively-researched weakness in the WSN
literature: a malicious or misbehaving node can fabricate low-cost
pheromone signals to attract traffic (then drop/tamper with it — a
"black-hole" attack) or fabricate high-cost signals to starve a healthy
region of traffic. Defenses exist there (reputation-weighted pheromone,
e.g. "A Multi-Attribute Pheromone Ant Secure Routing Algorithm Based on
Reputation Value," PMC5375827; "Secure Routing Protocol based on
Multi-objective ACO," ScienceDirect). **Not found anywhere:** this attack
class applied to a *query executor* rather than a packet router — where
the payoff for an attacker is different and arguably worse (a poisoned
node could route sensitive query traffic toward itself for exfiltration,
not just degrade throughput, and could poison join-order pheromone to
force expensive plans as a low-effort DoS). This is directly in the same
register as your AntChain project's own precedent — that project already
did a strategic/adversarial analysis of
pheromone-hoarding and withholding miners (Gini coefficient, strategic-
miner-advantage metric) for a *different* ant-colony-flavored mechanism.
The same lens — "what's the reward for a rational node that lies about
its local state?" — applied to AntSQL would make a natural companion
paper or companion section, and reuses methodology you've already built
rather than starting from zero.

### 9.3 Regret bounds under churn — the harder, more prestigious version of H1

§0.4's H1 is empirical (find λ\* in simulation). A stronger version:
derive λ\* analytically. SkinnerDB's whole framing win was "regret-bounded"
rather than "empirically fast" — regret defined as actual cost minus an
oracle's cost, bounded as a function of problem parameters (SkinnerDB
bounds it in terms of table count and time slices). General RL regret
theory gives lower bounds like Ω(√(D·X·A·T)) (D = MDP diameter, X = state
space, A = action space, T = horizon) — nothing found combines this with
a *churn process* (state space itself changing at rate λ) or with
*decentralization* (no single agent observes the full state). **This looks
genuinely open** — general searches for "regret bound distributed query
processing churn" return only tangential RL-theory papers, nothing on
point. High risk (this is a real theory contribution, not a systems one —
different skill, longer timeline) but if it lands, "AntSQL: an
adaptive-decentralized executor with a provable regret bound under
bounded churn rate" is a much harder paper to reject than a pure
simulation result, and it would make H1's predicted λ\*-scaling relationship
(§0.4) a *proven* relationship instead of an empirical observation you
hope holds.

### 9.4 Gossip-propagated pheromone — a deliberate middle point on axis 2

§0.3's map has a gap between AntNet's pure locality (a node only ever
learns from ants that actually traversed it) and SwAP/centralized-adaptive
(a coordinator, or at least broad inter-site exchange). Real production
distributed databases (Cassandra, DynamoDB, CockroachDB) use **gossip**
for membership/state dissemination — not full centralization, not pure
locality, but bounded-hop epidemic propagation. Hybrid pheromone+gossip
protocols exist in WSN routing generally (mixed push/pull gossip,
pheromone-weighted route metrics) but nothing found combines "gossip
propagates a decaying pheromone trail beyond one hop, at bounded cost"
specifically for query execution. This matters because it's the
*deployable* answer: pure AntNet-style locality is a clean research result
but a hard sell to an engineer who knows their production DB already runs
gossip for membership — showing where on the locality-to-full-broadcast
spectrum the crossover in §0.4's H1 actually sits (does 1-hop gossip
already capture most of the benefit? 2-hop? full flood?) turns the paper
from "decentralized beats centralized" into a practical dial an operator
could actually tune, which is a stronger contribution to reviewers who've
shipped real distributed databases.

### 9.5 Heterogeneous evaporation — hot/cold data gets different pheromone dynamics

AntNet used one fixed ant-generation-interval, shown robust across ~3
orders of magnitude (§2a) — but that's under roughly uniform traffic.
A sharded OLTP+OLAP database has genuinely heterogeneous access patterns:
hot partitions need fast-evaporating pheromone (stale trail = wrong
answer within seconds) while cold/archival partitions could use
slow-evaporating trail (rebuilding it constantly is pure waste). This is
a cheap, concrete ablation to run inside the main experiment rather than a
separate paper: does per-partition adaptive evaporation rate beat a single
global constant, and by how much, specifically under the UP-HS-style
hot-spot traffic pattern AntNet already validated as a good stress
condition (§2a)?

### 9.6 Cold-start: what happens in the first 500 seconds

AntNet's own protocol gives every algorithm 500 simulated seconds of
pure ant traffic before any data flows (§2a) — a real system doesn't get
that luxury after every deploy or failover. Worth an explicit experiment,
not just a caveat: pure cold-start (empty pheromone tables) vs.
**seeded** cold-start (bootstrap pheromone from one cheap static
plan/heuristic, then let reinforcement take over) vs. the adaptive-
centralized baseline's own cold-start cost (it also needs to gather
statistics before its first reoptimization — this isn't free either).
This closes an obvious reviewer question ("fine, it wins once it's
learned — how long does learning take, and is that time also a cost
against the centralized baseline?") before it gets asked.

### 9.7 Motivating deployment story: pick one, don't gesture at "distributed databases" broadly

AntNet's WAN-routing framing had one obvious deployment (the early
Internet, no single AS sees the whole topology). AntSQL needs an
equally concrete one, or reviewers will read the motivation as generic.
Two real candidates, genuinely different in their churn profile and worth
picking explicitly rather than leaving vague:
- **Multi-cloud/multi-region federated querying** — org-silo boundaries
  mean no control plane has full visibility across providers/regions by
  *policy*, not just engineering laziness; churn = region failover,
  cross-provider latency drift, new data sources appearing. Fits the
  2025-2026 "data mesh / federated lakehouse" narrative.
- **Edge/IoT database federations** — genuinely unreliable links,
  intermittent connectivity, no persistent control-plane connection
  possible; closer to Bonfils & Bonnet's original sensor-network setting,
  and a domain where "no node has global topology" is a *hard constraint*
  (not a design choice), which makes AntSQL's assumptions look necessary
  rather than self-imposed.
These have different λ\* regimes and different acceptable cold-start costs
(9.6) — the paper is stronger picking one as primary and mentioning the
other as a second validation domain than trying to motivate both equally.

## Open items before the experimental-design pass

- Confirm exact bibliographic metadata (full author lists, years, venues)
  for every citation in §1, §2, §3, §4 above before they go into a real
  .bib — several were found via ResearchGate/IGI abstract pages and need
  the canonical publisher record. (§2a's AntNet citation is now verified
  from the primary source and ready to cite as-is.)
- Decide whether to build the drift/churn generator from scratch or adapt
  DriftBench's — recommend at least reading its drift model before deciding.
- **Superseded by §0.5** — the priority reading list is now Bonfils &
  Bonnet (2003) and SwAP (2003) first, specifically to determine whether
  either already tested topology/node churn (if so, AntSQL's novelty
  claim narrows to "the benchmark + stigmergic mechanism," not "the
  decentralized mechanism"). The AntNet failure follow-ons are now
  priority 5, useful mainly for metric definitions once the above is
  resolved.
- **Do not run the crossover benchmark against a static-stats baseline
  only.** Per §0.4, the required control is an adaptive-*centralized*
  baseline (Eddies/SkinnerDB/SwAP/Borealis-style coordinator
  reoptimization) plus a decentralized-*non-stigmergic* ablation
  (greedy local search, à la Bonfils & Bonnet, or DHT-routed-non-learning,
  à la PIER). Three or four conditions minimum, not two — see §0.4 for
  why a two-condition (AntSQL vs. static optimizer) design produces a
  result that's already known and won't survive review.
