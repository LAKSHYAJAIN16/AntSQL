# AntSQL

AntSQL is my federated SQL gateway for edge and multi-site PostgreSQL
shards — routing queries to the right shard using ant-colony-style
pheromone feedback instead of a static routing table. The Python simulator
in `sim/` is where I originally designed and validated the algorithm;
`engine/` is the C++23 production-routing core I'm building on top of that
research.

## Where the C++ core is right now

The router does local pheromone route selection, cost-sensitive success
feedback, failure penalties, evaporation, exploration, and loop
prevention. There's an in-process forwarder and a mutable in-memory
topology for a dependency-free native multi-gateway test harness, plus a
second `IForwarder` implementation — `TcpForwarder`/`TcpServer` — that
forwards the same routing contract over real TCP sockets (Winsock on
Windows) with a hand-rolled, bounds-checked wire format. That means
multi-process/multi-machine routing and failure behavior can be exercised
today, without needing a gRPC or Arrow Flight SQL toolchain. `TcpServer`
serves connections concurrently (one thread per connection) so
`TcpForwarder` can cache and reuse a socket per neighbor instead of paying
a fresh handshake every call — it retries once on a new connection if a
cached one turns out stale. `Router` itself is thread-safe (internal
mutex), so one shared instance can absorb concurrent route decisions and
pheromone updates from multiple connection threads. A future gRPC/Flight
SQL forwarder would implement the same `IForwarder` interface and could
swap in without touching `Gateway` or the routing contract.

`SimpleSqlParser` is a real (deliberately narrow) `ISqlParser` — hand-written
recursive descent over single-table SELECT/INSERT/UPDATE/DELETE with at
most one integer-equality distribution-key predicate. It's standing in for
a real PostgreSQL-grammar parser like libpg_query, which isn't installable
in this environment either.

You can build and run all of it directly with clang++:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/tests/router_test.cpp engine/tests/gateway_test.cpp engine/tests/in_process_forwarder_test.cpp engine/tests/sql_test.cpp engine/tests/simple_sql_parser_test.cpp engine/tests/wire_test.cpp engine/tests/tcp_forwarder_test.cpp -lws2_32 -o engine-router-tests.exe
.\engine-router-tests.exe
```

Next up: Arrow Flight SQL, gRPC forwarding, a libpq shard executor, and
connection pooling for the TCP transport.

## `TableStoreExecutor`: an actual embedded storage engine

`IShardExecutor` has a real implementation now, not just test fakes.
`TableStoreExecutor` is an embedded storage engine — each table is a map
from its integer distribution key to a row of named columns, held in
memory and mirrored to an append-only write-ahead log
(`ReplayWal`/`AppendToWal`) so a node's data survives a restart. It
executes `SimpleSqlParser`'s SQL subset directly with its own small
tokenizer, since `ParsedStatement` deliberately only carries routing
facts, not column/value data — routing and execution are different
concerns and I wanted them to evolve independently. This is the thing that
turns AntSQL from "a router sitting in front of someone else's database"
into an actual database node — the bytes a client reads back were written
here.

`Gateway::Execute` also measures real wall-clock latency at both the leaf
(`shard_executor_.Execute`) and hop (`forwarder_.Forward`) call sites and
reports it as `QueryResponse::elapsed_ms`, instead of relying on a
caller-supplied placeholder. That's what makes the router's
"cost-sensitive success feedback" (`Router::ObserveSuccess`) actually
cost-sensitive end to end, not just in the Python simulator.

## The colony demo: a real multi-node ant-colony database, live

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/simple_sql_parser.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/src/table_store_executor.cpp engine/src/colony_demo_main.cpp -lws2_32 -o antsql-colony-demo.exe
.\antsql-colony-demo.exe
```

This boots a real 5-node ring — each node has its own `Router`, its own
`TableStoreExecutor`, and its own `TcpServer` on a real loopback socket,
wired to its two ring neighbors over real `TcpForwarder`/TCP connections.
No node ever sees the whole ring. What the demo does:

1. Inserts one row and reads it back 40 times from a node three hops away
   in either direction. Nothing tells the colony which way is shorter, so
   the two symmetric routes compete purely on positive feedback (the same
   take-off dynamic as the classic ant double-bridge experiment) until one
   wins and every gateway independently commits to it.
2. Prints the resulting pheromone trail on every ring edge.
3. Kills the node on the colony's current favorite path mid-run. Success
   rate dips, then recovers within a handful of queries as pheromone
   drains off the dead edge (`Router::ObserveFailure`) and builds on the
   surviving path — no coordinator ever recomputes a global plan. A
   per-tick trace gets written to `results/colony_demo_run.csv`, the same
   convention the paper's resilience studies
   (`results/resilience_study_v3_paired.csv`) already use.
4. Reads the row directly off the owning shard's own storage engine,
   bypassing the ring entirely, just to confirm the data is real and lives
   where the routing protocol says it does.

This is the same mechanism I argue (in `research/related_work.md` §0.3) is
still an open combination in the literature — decentralized,
no-global-topology query execution using persistent reinforced-trail
credit assignment — and here it's running as actual code against actual
sockets and actual stored rows, not a discrete-event simulation.

## Deployment scaffold

[`deploy/`](deploy/) has a Docker Compose stack for three PostgreSQL 16
shards plus its init schema. It's ready to provision the database cluster
once Docker Desktop is installed, but it doesn't expose a deployable
AntSQL gateway yet — that needs the pending libpq, Arrow Flight SQL, and
gRPC adapters.

## `service/`: AntSQL Cloud — a separate, runnable product

[`service/`](service/) is a Node/JavaScript reimplementation of the
routing algorithm (`server/router.js` is a line-for-line port of
`engine/include/antsql/router.hpp`), applied to adaptive replica selection
for a Firestore-simple document database — collections, documents, one API
key as the entire signup flow. It's a separate deployable surface from the
C++ research engine, not a wrapper around it. Real HTTP API, real
in-memory storage mirrored to a write-ahead log, real
routing/retries/failure recovery, and a website with a live playground and
a fail/heal demo of the colony:

```bash
cd service
node server/index.js
# AntSQL Cloud listening on http://localhost:4280
npm test
```

See [`service/README.md`](service/README.md) for the full API and what's
simulated vs. real.

## `sim/`: the Python research model

[`sim/`](sim/) is the original discrete-event simulator I designed and
validated the routing algorithm and its ablations against (hop-budget
sweeps, failure-mode ablations, resilience studies) before reimplementing
it natively in `engine/`. Entry points: `run_experiment.py`,
`run_scale.py`, `run_failure_mode_ablation.py`, `run_hop_budget_sweep.py`,
`run_resilience_study_paired.py`, `calibrate.py`,
`diagnose_convergence.py`. See [`paper/antsql_paper.md`](paper/antsql_paper.md)
and [`research/related_work.md`](research/related_work.md) for the
experimental protocol and how this positions against prior work.
