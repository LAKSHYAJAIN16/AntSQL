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

## Current blockers and next work

- The current session has clang++ but lacks CMake, Ninja, PostgreSQL/libpq,
  Arrow Flight SQL, gRPC, and a PostgreSQL parser library.
- An attempt to install CMake/Ninja/PostgreSQL was blocked because the Windows
  session is not elevated.
- Next implementation layer after those dependencies are available: libpq
  shard adapter, PostgreSQL parser adapter, Flight SQL endpoint, and internal
  gRPC forwarding. The in-process forwarding harness is now available for
  routing/failure integration tests in the meantime.
