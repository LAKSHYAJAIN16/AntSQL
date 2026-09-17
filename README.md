# AntSQL

> A federated SQL gateway that routes queries using ant-colony pheromone feedback instead of a static routing table.

I built this for edge and multi-site PostgreSQL shards. `sim/` is the Python simulator where I designed and validated the algorithm; `engine/` is the C++23 production-routing core built on top of that research; `service/` is a separate deployable product applying the same algorithm to a document database.

## `engine/`: the C++ routing core

`Router` does local pheromone route selection, cost-sensitive success feedback, failure penalties, evaporation, exploration, and loop prevention — thread-safe, so one instance handles concurrent route decisions from multiple connections. Two `IForwarder` implementations exist: an in-process one for dependency-free tests, and `TcpForwarder`/`TcpServer` which forwards the same routing contract over real TCP sockets (Winsock on Windows), so multi-process/multi-machine routing and failure behavior work today without gRPC or Arrow Flight SQL.

`SimpleSqlParser` is a real (deliberately narrow) `ISqlParser` — hand-written recursive descent over single-table SELECT/INSERT/UPDATE/DELETE with at most one integer-equality distribution-key predicate, standing in for a real PostgreSQL-grammar parser.

`TableStoreExecutor` is a real embedded storage engine, not a test fake: each table is an in-memory map from integer distribution key to a row of named columns, mirrored to an append-only WAL so data survives a restart. `Gateway::Execute` measures real wall-clock latency at both the leaf and hop call sites, so the router's cost-sensitive feedback is cost-sensitive end to end, not just in the Python simulator.

Build and run the tests:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/tests/router_test.cpp engine/tests/gateway_test.cpp engine/tests/in_process_forwarder_test.cpp engine/tests/sql_test.cpp engine/tests/simple_sql_parser_test.cpp engine/tests/wire_test.cpp engine/tests/tcp_forwarder_test.cpp -lws2_32 -o engine-router-tests.exe
.\engine-router-tests.exe
```

Next up: Arrow Flight SQL, gRPC forwarding, a libpq shard executor, connection pooling.

### Colony demo: a real multi-node ant-colony database, live

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/src/table_store_executor.cpp engine/src/colony_demo_main.cpp -lws2_32 -o antsql-colony-demo.exe
.\antsql-colony-demo.exe
```

Boots a real 5-node ring, each with its own `Router`, `TableStoreExecutor`, and `TcpServer`, wired to its two ring neighbors over real TCP. No node sees the whole ring. The demo inserts a row, reads it 40 times from three hops away in either direction and watches the colony converge on one route by pure positive feedback, kills the node on the winning path and watches success rate recover as pheromone reroutes around the failure, then reads the row directly off the owning shard to confirm it's real. Per-tick trace written to `results/colony_demo_run.csv`.

## `service/`: AntSQL Cloud

[`service/`](service/) is a Node reimplementation of the routing algorithm (`server/router.js` ports `engine/include/antsql/router.hpp`) applied to adaptive replica selection for a Firestore-simple document database. Separate deployable surface, not a wrapper around the C++ engine — real HTTP API, real WAL-backed storage, real routing/retries/failure recovery, plus a website with a live playground and fail/heal demo.

```bash
cd service
node server/index.js   # AntSQL Cloud on http://localhost:4280
npm test
```

See [`service/README.md`](service/README.md) for the full API.

## `sim/`: the Python research model

The original discrete-event simulator used to design and validate the routing algorithm and its ablations (hop-budget sweeps, failure-mode ablations, resilience studies) before the native `engine/` port. Entry points: `run_experiment.py`, `run_scale.py`, `run_failure_mode_ablation.py`, `run_hop_budget_sweep.py`, `run_resilience_study_paired.py`, `calibrate.py`, `diagnose_convergence.py`. See [`paper/antsql_paper.md`](paper/antsql_paper.md) and [`research/related_work.md`](research/related_work.md) for the experimental protocol and prior-work positioning.

## `deploy/`

[`deploy/`](deploy/) has a Docker Compose stack for three PostgreSQL 16 shards plus init schema — ready to provision, but doesn't expose a deployable AntSQL gateway yet (needs the pending libpq/Flight SQL/gRPC adapters).
