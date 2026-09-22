# Scope: making AntSQL usable by other people

Two tracks. Track A (AntSQL Cloud, the hosted document database) is mostly
done and only needs the deploy. Track B (the C++ engine as a real SQL
gateway in front of PostgreSQL) is the bigger job and is scoped here in
phases, each shippable on its own.

Status as of 2026-09-22.

---

## Track A — AntSQL Cloud, hosted

**Goal:** anyone can open a URL, get an API key, and use it as the database
behind a small app, from curl, Node, or a browser `<script>` tag.

### Done

| What | Why it mattered |
|---|---|
| Tenant-scoped `/v1/_colony/stats` | It returned every tenant's route keys, which embed their **API keys**. Any user could read everyone's credentials. Blocker for hosting. |
| Tenant-scoped replica fail/heal | One user's demo outage took replicas down for every user. |
| WAL-durable heal resync | A healed replica's catch-up lived only in memory; a restart replayed stale data. |
| Body size cap (256 KB), JSON-object bodies, name validation | Unbounded bodies were a free memory/disk-fill attack on a public host. |
| Per-key document quota (`MAX_DOCS_PER_KEY`) | Everything is in memory; one key could exhaust the machine. |
| Proxy-aware client IP (`TRUST_PROXY=1`) | Behind a host's proxy every user shared one IP, so the 5-keys/min signup limit would have been global. |
| WAL compaction on startup | The log grew forever on a long-running host. |
| Queries: `where`, `orderBy`, `limit`, cursor pagination (API + SDK) | Before this, reading a collection meant downloading all of it. |
| SDK served from the host, defaults to its origin | A browser app needs one script tag and a key. |
| `/healthz`, SIGTERM handling, Dockerfile, `fly.toml` | Deployable. |

### Remaining to go live

1. `fly auth login` (needs the account owner, plus a card on file).
2. `fly launch --no-deploy --copy-config`, `fly volumes create antsql_data --size 1`, `fly deploy`.
3. Smoke test the public URL: key → write → query → fail/heal from the website playground.

**Expected cost:** one shared-cpu-1x/256 MB machine that scales to zero when
idle, plus a 1 GB volume. Low single-digit dollars a month at hobby traffic;
check Fly's current pricing page.

### Known limits of the hosted service (be upfront about these)

- **Replicas are simulated.** The 3 replicas per shard live in one process
  with injected latency. The routing and failover are real, but a machine
  crash takes all of them down together. This is a demo of the algorithm
  wearing a useful API, not a highly available database.
- **Single machine, single region.** Data is safe on the volume; the
  learned pheromone is in memory and relearns in a few reads after a cold
  start.
- **Queries scan.** Filters run over the collection after reading it; fine
  up to the per-key quota, not an index.
- **No backups yet.** See next steps.

### Next steps after launch (in priority order)

1. **Nightly volume snapshot** (`fly volumes snapshots` are automatic daily;
   verify retention) and a documented restore.
2. **Real replicas.** Run each replica as its own Fly machine in a
   different region and route over HTTP. This is where the colony
   stops being a demo: real cross-region latency differences for it to
   learn, and real independent failures to route around.
3. **Transactions / batched writes** (`POST /v1/db/_batch`), the most
   requested Firestore feature after queries.
4. **Secondary indexes** for `where` on declared fields.

---

## Track B — The engine as a PostgreSQL gateway

**Goal:** an app points its normal Postgres driver (or `psql`) at an AntSQL
node. AntSQL routes each query by its distribution key to the PostgreSQL
shard that owns it, across a mesh of AntSQL nodes, adapting routes by
pheromone when a shard or link degrades.

**Starting point (verified):** `Router`, `Gateway`, `TcpForwarder`/
`TcpServer` (already portable: Winsock and POSIX sockets), `SimpleSqlParser`
(single-table SELECT/INSERT/UPDATE/DELETE with one integer equality on the
distribution key), `TableStoreExecutor` (embedded store, not Postgres), and
`deploy/docker-compose.yml` with three Postgres 16 shards and the `readings`
schema keyed on `site_id`. All tests pass.

### Phase 1 — Structured results (prerequisite, ~2-3 days)

`QueryResponse::payload` is one opaque string. Postgres clients need
column names, type OIDs, and rows. Add
`struct ResultSet { std::vector<Column> columns; std::vector<std::vector<std::optional<std::string>>> rows; std::string command_tag; }`
to `QueryResponse`, carry it through `wire.cpp` for hop-to-hop forwarding,
and migrate `TableStoreExecutor` and the tests.

*Done when:* a SELECT forwarded three hops returns typed columns and rows,
not a string.

### Phase 2 — `PgShardExecutor` over libpq (~1 week)

An `IShardExecutor` that runs the validated statement against the owning
Postgres shard:

- libpq connection pool per shard (fixed size, health-checked, reconnect
  with backoff); `PQexecParams`, never string-spliced SQL.
- Map libpq errors to `QueryStatus` (`ShardUnavailable` on connection
  failure, `ExecutionError` on SQL errors) so the router's failure penalty
  sees real shard failures.
- Real leaf latency is already measured by `Gateway::Execute`, so
  cost-sensitive routing works against actual Postgres timings for free.

*Done when:* an integration test against the compose shards inserts and reads
`readings` rows through the engine, and killing a shard container yields
`ShardUnavailable` and a pheromone drop.

### Phase 3 — PostgreSQL wire protocol frontend (~1.5-2 weeks)

A listener that speaks protocol v3 so real clients connect:

- **3a, simple query protocol:** startup, SSLRequest refusal,
  cleartext/SCRAM-SHA-256 auth, `Query`, `RowDescription`/`DataRow`/
  `CommandComplete`/`ReadyForQuery`, `ErrorResponse` with SQLSTATE,
  `Terminate`. This gets `psql` and psycopg working.
- **3b, extended protocol:** `Parse`/`Bind`/`Describe`/`Execute`/`Sync`
  with `$1` parameters. Needed by node-postgres, JDBC, pgx, and nearly
  every ORM. The parser must extract the distribution key from a bound
  parameter, not just a literal.
- Unsupported statements (joins, multi-shard scans, DDL, `BEGIN` spanning
  shards) return a clear `0A000 feature_not_supported`, never a wrong answer.

*Done when:* `psql -h localhost -p 6432` and a node-postgres script both run
the demo workload end to end.

### Phase 4 — Configuration and packaging (~3-5 days)

- One config file per node: node id, listen addresses, owned
  `(table, partition range) → Postgres DSN`, neighbor addresses, hop
  budget, router tuning. Replaces hand-built `InMemoryTopology` in demos.
- `antsqld` binary (Linux + Windows via CMake), Docker image, and
  compose wiring: 3 AntSQL nodes in front of the existing 3 shards.

*Done when:* `docker compose up` gives a working 3-node gateway, and the
README quickstart is copy-paste runnable.

### Phase 5 — Proof it's worth using (~1 week, feeds the paper)

Benchmark the compose stack against a static-routing baseline (same
engine, pheromone updates disabled) under injected shard latency and
container kills (`tc netem`, `docker kill`). Report p50/p99 latency and
success rate. This is the real-system counterpart of the simulator's
paired resilience study and the result that makes the paper credible.

### Explicitly out of scope (for now)

- Queries without an exact distribution-key predicate (scatter-gather).
- Cross-shard transactions and joins.
- Replacing `SimpleSqlParser` with libpg_query. Worth doing after Phase 3,
  once the protocol surface is stable; broadens what's routable, not
  whether it works.
- Arrow Flight SQL / gRPC forwarding. The existing TCP forwarder is
  sufficient; revisit only if Phase 5 shows forwarding overhead dominates.

### Total

About **5-7 weeks** of focused work to reach "point your Postgres driver at
it." Phases 1-2 alone (about 1.5 weeks) give a usable C++ library that
routes real Postgres queries, which is enough for the Phase 5 experiment
if the paper is the priority.
