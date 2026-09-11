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
