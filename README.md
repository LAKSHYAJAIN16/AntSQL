# AntSQL

AntSQL is a federated SQL gateway for edge and multi-site PostgreSQL shards.
The Python simulator in `sim/` is the research model; `engine/` is the C++23
production-routing core.

## Current C++ milestone

The core implements local pheromone route selection, cost-sensitive success
feedback, failure penalties, evaporation, exploration, and loop prevention.
An in-process forwarder and mutable in-memory topology provide a
dependency-free native multi-gateway harness for single-process tests. A
second `IForwarder` implementation, `TcpForwarder`/`TcpServer`, forwards the
same routing contract over real TCP sockets (Winsock on Windows) with a
hand-rolled, bounds-checked wire format — so multi-process/multi-machine
routing and failure behavior can be exercised today without a gRPC or Arrow
Flight SQL toolchain. `TcpServer` serves connections concurrently (one
thread per connection) so `TcpForwarder` can cache and reuse one socket per
neighbor instead of paying a fresh handshake on every call, retrying once
on a fresh connection if a cached one turns out to be stale. `Router` is
thread-safe (an internal mutex) so the same shared instance can absorb
concurrent route decisions and pheromone updates from multiple connection
threads. A future gRPC/Flight SQL forwarder implements the same
`IForwarder` interface and can replace either without changing `Gateway` or
the routing contract. `SimpleSqlParser` is a real (deliberately narrow)
`ISqlParser` implementation — hand-written recursive descent over
single-table SELECT/INSERT/UPDATE/DELETE with at most one integer-equality
distribution-key predicate — standing in for a PostgreSQL-grammar parser
such as libpg_query, which is not installable in this environment either.
It can all be tested with clang++ directly:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/tests/router_test.cpp engine/tests/gateway_test.cpp engine/tests/in_process_forwarder_test.cpp engine/tests/sql_test.cpp engine/tests/simple_sql_parser_test.cpp engine/tests/wire_test.cpp engine/tests/tcp_forwarder_test.cpp -lws2_32 -o engine-router-tests.exe
.\engine-router-tests.exe
```

Arrow Flight SQL, gRPC forwarding, a libpq shard executor, and connection
pooling for the TCP transport are the next layers built on this core.

## `TableStoreExecutor`: a real embedded storage engine

`IShardExecutor` now has a real implementation, not just test fakes:
`TableStoreExecutor` is an embedded storage engine — each table is a map
from its integer distribution key to a row of named columns, held in memory
and mirrored to an append-only write-ahead log (`ReplayWal`/`AppendToWal`)
so a node's data survives a restart. It executes SimpleSqlParser's SQL
subset directly (its own small tokenizer, since `ParsedStatement`
deliberately carries only routing facts, not column/value data — routing
and execution are different concerns and can now evolve independently).
This is what turns AntSQL from a router sitting in front of someone else's
database into an actual database node: the bytes a client reads back were
written here.

`Gateway::Execute` also now measures real wall-clock latency at both the
leaf (`shard_executor_.Execute`) and hop (`forwarder_.Forward`) call sites
and reports it as `QueryResponse::elapsed_ms`, instead of relying on a
caller-supplied placeholder value. This is what makes the router's
"cost-sensitive success feedback" (`Router::ObserveSuccess`) actually
cost-sensitive end to end, not just in the Python simulator.

## Colony demo: a real multi-node ant-colony database, live

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/src/table_store_executor.cpp engine/src/colony_demo_main.cpp -lws2_32 -o antsql-colony-demo.exe
.\antsql-colony-demo.exe
```

Boots a real 5-node ring: each node is its own `Router`, its own
`TableStoreExecutor`, and its own `TcpServer` on a real loopback socket,
wired to its two ring neighbors over real `TcpForwarder`/TCP connections —
no node ever sees the whole ring. The demo:

1. Inserts one row and reads it back 40 times from a node three hops away
   in either direction; nothing tells the colony which way is shorter, so
   the two symmetric routes compete purely on positive feedback (the same
   take-off dynamic as the classic ant double-bridge experiment) until one
   wins and every gateway independently commits to it.
2. Prints the resulting pheromone trail on every ring edge.
3. Kills the node on the colony's current favorite path mid-run. Success
   rate dips, then recovers within a handful of queries as pheromone drains
   off the dead edge (`Router::ObserveFailure`) and builds on the surviving
   path — with no coordinator ever recomputing a global plan. A per-tick
   trace is written to `results/colony_demo_run.csv`, the same convention
   the paper's resilience studies (`results/resilience_study_v3_paired.csv`)
   already use.
4. Reads the row directly off the owning shard's own storage engine,
   bypassing the ring, to confirm the data is real and lives where the
   routing protocol says it does.

This is the same mechanism the paper's `research/related_work.md` (§0.3)
argues is still an open combination in the literature — decentralized,
no-global-topology query *execution* using persistent reinforced-trail
credit assignment — running as actual code against actual sockets and
actual stored rows, not a discrete-event simulation.

## Deployment scaffold

[`deploy/`](deploy/) contains a Docker Compose stack for three PostgreSQL 16
shards and its initialization schema. It is ready to provision the database
cluster after Docker Desktop is installed, but it does not yet expose a
deployable AntSQL gateway: that requires the pending libpq, Arrow Flight SQL,
and gRPC adapters.

## `service/`: AntSQL Cloud — a separate, runnable product

[`service/`](service/) is a Node/JavaScript reimplementation of the routing
algorithm (`server/router.js` is a line-for-line port of
`engine/include/antsql/router.hpp`), applied to adaptive replica selection
for a Firestore-simple document database — collections, documents, one API
key as the entire signup flow. It is a separate deployable surface from the
C++ research engine, not a wrapper around it. Real HTTP API, real in-memory
storage mirrored to a write-ahead log, real routing/retries/failure
recovery, and a website with a live playground and a fail/heal demo of the
colony:

```bash
cd service
node server/index.js
# AntSQL Cloud listening on http://localhost:4280
npm test
```

See [`service/README.md`](service/README.md) for the full API and
what's simulated vs. real.

## `sim/`: the Python research model

[`sim/`](sim/) is the original discrete-event simulator the routing
algorithm and its ablations (hop-budget sweeps, failure-mode ablations,
resilience studies) were designed and validated against before being
reimplemented natively in `engine/`. Entry points: `run_experiment.py`,
`run_scale.py`, `run_failure_mode_ablation.py`,
`run_hop_budget_sweep.py`, `run_resilience_study_paired.py`,
`calibrate.py`, `diagnose_convergence.py`. See
[`paper/antsql_paper.md`](paper/antsql_paper.md) and
[`research/related_work.md`](research/related_work.md) for the experimental
protocol and how this project positions itself against prior work.
