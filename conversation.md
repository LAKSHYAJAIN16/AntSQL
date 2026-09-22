# AntSQL conversation history

This is a concise project history, preserved alongside the source tree.

## Research direction

- AntSQL began as a proposal for decentralized ant/pheromone-inspired SQL
  routing under topology and workload churn.
- Related-work review identified Eddies, SkinnerDB, SwAP, and Bonfils & Bonnet
  as essential prior art. The novelty claim was narrowed: AntSQL must compare
  against an adaptive centralized baseline and isolate persistent stigmergic
  credit assignment from generic adaptation/decentralization.
- The central hypothesis became a churn crossover, lambda-star: centralized
  coordination wins while its state is fresh; local reinforced routing may win
  when churn outpaces observation and broadcast.

## Simulator work

- Built a Python discrete-event simulator with static centralized, adaptive
  centralized, decentralized greedy, and AntSQL routing conditions.
- Added topology/workload/churn events, recovery/control-overhead metrics,
  calibration and scale runners, and routing regression tests.
- Found and corrected flat reinforcement and gossip-overwrite issues.
- Preliminary 14-node/three-seed study: AntSQL loses stable-network tail
  latency but improves completion at severe compound churn (0.690 versus 0.457
  adaptive-centralized success at churn 0.20).
- A 1,000-node flat simulation failed to scale, motivating a hierarchical
  gateway overlay.
- Reran the resilience study at 20 seeds (up from 3) across five churn
  rates (0.00-0.30, up from four), all four conditions including the
  decentralized-greedy ablation. Findings revised from the 3-seed study:
  AntSQL decisively beats decentralized-greedy at every non-zero churn rate
  (isolating stigmergic credit assignment as the source of its advantage,
  not decentralization alone), but the AntSQL-vs-adaptive-centralized
  crossover is a bounded window (roughly churn 0.10-0.20), not a monotonic
  advantage at high churn — AntSQL loses again at churn 0.30. Variance at
  high churn is large even at 20 seeds. See `paper/antsql_paper.md` §5-6 and
  `results/resilience_study_v2.csv` / `resilience_study_v2_summary.csv`.
- Verified the SwAP and Bonfils & Bonnet citations flagged as unverified in
  `research/related_work.md`; both are now cited precisely in the paper's
  references.
- Ran a per-failure-mode ablation (`sim/run_failure_mode_ablation.py`):
  isolated each churn arm (node death, node revival, shard migration,
  latency shift, workload shift) at churn 0.20/0.30, 20 seeds, all four
  strategies. Latency/workload shift alone move no strategy's success rate;
  node death/revival degrade all four similarly; shard migration alone is
  where adaptive strategies (both centralized and AntSQL) pull ahead of
  static/greedy ones, and AntSQL is not behind adaptive-centralized on any
  single arm. Conclusion: the churn-0.30 reversal from the compound study is
  an interaction effect, not attributable to one arm — added as
  `paper/antsql_paper.md` §7, with a sharper, testable next step (vary
  AntSQL's hop budget) replacing the original vaguer "which arm" question.
- Ran that hop-budget follow-up (`sim/run_hop_budget_sweep.py`): swept
  `max_query_hops` in {4,6,8,10,15,25} at churn 0.20/0.30, 20 seeds,
  adaptive-centralized vs. AntSQL only. Came back inconclusive — the
  AntSQL-minus-adaptive gap didn't move monotonically with hop budget, and
  per-cell standard deviations (0.10-0.20) dwarfed the differences between
  hop budgets. Reported honestly in the paper as a null result rather than
  dropped: the hop-budget/multi-hop-fragility hypothesis is not confirmed,
  and the churn-0.30 interaction is still unexplained.
- Found the actual cause of the noise that swamped the hop-budget sweep,
  and it turned out to reopen the paper's central claim: `experiment.run_sweep`
  and `run_failure_mode_ablation.py` both derive each trial's seed from a
  hash that includes `strategy_name`, so every strategy in every sweep so
  far (including the main resilience study) drew an independently-random
  topology/shard-placement/workload/churn schedule even "at the same
  trial" — none of it was ever paired. Wrote `run_hop_budget_sweep.py` to
  derive its seed from `(churn_rate, trial)` only instead (all strategies
  and hop budgets at a trial now share one environment) and reran it: the
  gap resolved into a clean, monotonic, mostly-significant curve — AntSQL
  significantly worse than adaptive-centralized at hop budget 4-6,
  significantly better from 10 up, opposite direction from the original
  multi-hop-fragility hypothesis (budget starvation, not path-length
  fragility, per `paper/antsql_paper.md` §7). At hop budget 15 — the main
  study's default — the paired gap was positive at churn 0.30, contradicting
  the main study's unpaired finding that AntSQL loses there. That was
  reason enough to rerun the main resilience study itself paired
  (`sim/run_resilience_study_paired.py`, same 14-node/10-shard/400-tick/
  20-seed/5-churn-rate config as `resilience_study_v2.csv`, seed keyed only
  on `(churn_rate, trial)`). It reverses the churn-0.30 reversal: AntSQL
  ties adaptive-centralized at churn 0.00-0.05 and is significantly ahead
  at 0.10/0.20/0.30 (growing, not bounded), confirmed by a near-perfect
  r≈0.99 correlation between the two strategies' paired outcomes (almost
  all per-run variance is a shared-environment effect that pairing is
  designed to cancel, not strategy-specific noise). Checked for a mundane
  explanation before trusting this (no code changes to the simulator core
  since v2 was generated per git history; every RNG use is a seeded
  per-instance `numpy.random.default_rng`, not global state, so no
  cross-run contamination) — found none, so the discrepancy is attributed
  to attempt 2's unpaired design, not a bug. Rewrote the paper's abstract,
  §5 (results), §6 (now "three attempts at this result, and why the third
  is trusted"), §7 (framing), §8, and §9 accordingly. Raw data:
  `results/hop_budget_sweep_paired.csv` and `results/resilience_study_v3_paired.csv`;
  summaries alongside each. The per-failure-mode ablation (§7) still uses
  the unpaired seeding and has not been rerun — flagged in §8-9 as the
  next thing to redo before trusting its specific margins.
- Attempted TCP connection reuse in `TcpForwarder` (cache one socket per
  neighbor instead of connecting fresh every call) to reduce handshake
  overhead. A new test caught a real bug: `TcpServer` closes the connection
  after every single request, so the cached client-side socket is always
  already dead before a second use — there is nothing valid to reuse
  without first making `TcpServer` persistent-connection-aware. Doing that
  properly requires serving connections concurrently (one thread per
  connection instead of the current single accept-thread), which would
  expose `Router`'s pheromone map and RNG — currently unsynchronized — to
  real concurrent mutation for the first time. That is a separate,
  higher-risk change than "add connection reuse," so the pooling attempt
  was reverted back to the simple, already-tested one-connection-per-call
  design rather than shipped half-fixed or with a latent race condition.

## Engine: SQL parser adapter

- Added `SimpleSqlParser` (`engine/include/antsql/simple_sql_parser.hpp`,
  `engine/src/simple_sql_parser.cpp`): a real, hand-written recursive-descent
  `ISqlParser` implementation for AntSQL's v1 SQL subset (single-table
  SELECT/INSERT/UPDATE/DELETE, at most one integer-equality distribution-key
  predicate, decomposable-aggregate detection). Not a PostgreSQL-grammar
  parser — exists so the gateway has a real parser to route against before
  libpg_query is available in this environment; swappable later without
  touching `ISqlParser`/`SqlValidator`/`Gateway`. Verified with clang++
  directly, including a bug caught in the test itself (a string literal on
  the key column is valid SQL, not a parse error) before it was fixed.

## Engine: TCP connection reuse and Router thread-safety

- Picked back up the connection-reuse attempt that was reverted earlier
  (see above) by doing the prerequisite work it was blocked on:
  - `TcpServer` now serves connections concurrently — one worker thread per
    accepted connection, looping on request/response frames until the peer
    disconnects — instead of one request per connection on a single accept
    thread. `Stop()` was reordered to join the accept thread first (so no
    new connections can arrive), then close and join every still-open
    connection, closing the race where a connection accepted right at
    shutdown could be left unjoined.
  - `Router` gained an internal mutex; all public methods lock it, with a
    lock-already-held internal `PheromoneLocked` helper so `Choose()` (which
    calls `Score()` which calls `Pheromone()`) doesn't deadlock on its own
    non-recursive mutex. It is now safe for the same `Router` instance to be
    mutated concurrently from multiple `TcpServer` connection threads, which
    is exactly what a gateway under concurrent load does.
  - `TcpForwarder` now caches one reusable socket per neighbor (guarded by
    its own per-connection mutex, so concurrent forwards to *different*
    neighbors don't block each other, while forwards to the *same* neighbor
    serialize on that neighbor's socket — this transport doesn't pipeline).
    If a call using a previously-live cached socket fails, it's dropped and
    one fresh connection is attempted before reporting the neighbor
    unavailable, so a stale connection (e.g. the peer's `TcpServer` process
    restarted) self-heals on the next call instead of wedging.
  - Hit and fixed a real MSVC STL portability bug along the way:
    `std::unordered_map<std::string, std::unique_ptr<Connection>>` with
    `Connection` forward-declared in the header (the usual Pimpl pattern)
    failed to compile — MSVC's `unordered_map`, unlike `vector`, doesn't
    guarantee support for an incomplete value type, and eagerly instantiated
    the hash table's internals. Switched to a raw `Connection*` map with
    explicit `new`/`delete`, which sidesteps the guarantee entirely.
  - Added a Router concurrency stress test (8 threads, 500 iterations each,
    interleaving `Choose`/`ObserveSuccess`/`ObserveFailure`/`Evaporate` on
    one shared instance) and two new `TcpForwarder` tests: one asserting
    five round trips to the same neighbor land on a single accepted TCP
    connection (`TcpServer::ConnectionsAccepted()`), one asserting recovery
    after the peer's server is stopped and restarted on the same port.
    Verified by compiling and running the full suite with clang++ directly,
    five consecutive runs with no flakes.

## Production direction

- Product choice: C++23 federated SQL gateway, not a new storage engine.
- Durable shards: PostgreSQL.
- Client protocol: Apache Arrow Flight SQL.
- Workload focus: edge/federated reads under churn.
- V1 writes: single-shard transactions only.
- Initial real-system target: 32 PostgreSQL shards and 100 clients on native
  Windows.
- C++ milestones committed and pushed:
  - `7aeb27c` C++ pheromone routing core.
  - `c199b6b` gateway routing contract.
  - `4accea5` parser-agnostic SQL validation boundary.
  - Added an in-process `IForwarder` implementation and multi-gateway routing
    regression. It exercises a real gateway hop and unavailable-peer feedback
    without requiring gRPC, PostgreSQL, or libpq.
  - Added mutable in-memory topology state for the native harness and extended
    the regression to three gateways. Gateway reinforcement now applies only
    to the locally selected edge, preventing a relay from learning invalid
    pheromone entries for upstream hops.
  - Added a second `IForwarder` implementation, `TcpForwarder`/`TcpServer`,
    that forwards the same routing contract over real TCP sockets (Winsock)
    with a hand-rolled, bounds-checked wire format (`engine/src/wire.cpp`).
    This is dependency-free and exercises real serialization and socket I/O
    without needing gRPC/Arrow Flight SQL, which remain unavailable in this
    environment. Verified by compiling and running the full test suite with
    clang++ directly (not just reasoned about): all tests pass, including a
    real round trip over 127.0.0.1 and a stopped-server failure case.

## Current blockers and next work

- The current session has clang++ but lacks CMake, Ninja, PostgreSQL/libpq,
  Arrow Flight SQL, gRPC, and a PostgreSQL parser library.
- An attempt to install CMake/Ninja/PostgreSQL was blocked because the Windows
  session is not elevated.
- No local Docker, Kubernetes tooling, or PostgreSQL service is currently
  present. A `deploy/` Docker Compose scaffold now defines three PostgreSQL 16
  shards for when Docker Desktop is available; it is intentionally database-
  only until the gateway adapters are implemented.
- Next implementation layer after those dependencies are available: libpq
  shard adapter, PostgreSQL parser adapter, Flight SQL endpoint, and internal
  gRPC forwarding. The in-process forwarding harness is now available for
  routing/failure integration tests in the meantime.
- The connection-reuse blocker is resolved (see "Engine: TCP connection
  reuse and Router thread-safety" above): `TcpServer` is persistent-
  connection-aware and `Router` is thread-safe. Remaining dependency-free
  engine work in this area: `TcpForwarder`'s cached connection is one
  socket per neighbor, so concurrent calls to the same neighbor serialize
  rather than pipeline — a connection pool (multiple sockets per neighbor)
  would remove that serialization if a workload ever needs the throughput,
  but nothing in the current test/research harness has demonstrated that
  need yet.
