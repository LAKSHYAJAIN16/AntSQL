# AntSQL Cloud

A Firestore-simple document database, backed by AntSQL's ant-colony
pheromone-routing algorithm instead of a static load balancer. This is a
separate, deployable product surface — a Node/JavaScript reimplementation
of the routing algorithm from [`engine/`](../engine), applied to adaptive
replica selection — not a wrapper around the C++ research engine, which is
a native binary and a poor fit for a web service.

## Run it locally

```bash
cd service
node server/index.js
# AntSQL Cloud listening on http://localhost:4280
```

Open `http://localhost:4280` for the landing page, docs, and an interactive
playground, or drive it directly:

```bash
curl -X POST http://localhost:4280/v1/keys
# {"apiKey":"ant_..."}

curl -X POST http://localhost:4280/v1/db/users \
  -H "Authorization: Bearer ant_..." -H "Content-Type: application/json" \
  -d '{"name":"Ada","balance":100}'
```

Or with the SDK (`sdk/antsql-client.js`, zero dependencies, browser or
Node):

```js
const { AntSQL } = require('./sdk/antsql-client');
const apiKey = await AntSQL.createKey('http://localhost:4280');
const db = new AntSQL({ apiKey, baseUrl: 'http://localhost:4280' });
const ref = await db.collection('users').add({ name: 'Ada' });
console.log((await db.collection('users').doc(ref.id).get()).data());
```

Run the tests: `npm test` (spawns the real server, no mocks, including a
test that fails a replica mid-flight and checks reads still succeed).

## How it works

- **One API key = one isolated namespace.** Creating a key is the entire
  signup flow — `POST /v1/keys`, no email or account.
- **Sharding**: each document's key hashes to one of a fixed set of shards
  (`server/colony.js`). Each shard has 3 in-process replicas with
  deliberately different simulated latency.
- **Routing**: `server/router.js` is a line-for-line JS port of
  [`engine/include/antsql/router.hpp`](../engine/include/antsql/router.hpp)
  — same pheromone/evaporation/reinforcement/failure-penalty formulas.
  Every read picks a replica via the router, and if that replica is down,
  retries the next-best one *within the same request* (so a client never
  sees a failure as long as one replica survives) while still recording the
  failure so the colony's preference shifts.
- **Durability**: every write is appended to a per-replica write-ahead log
  before being acknowledged; a replica replays its WAL on startup, and a
  healed replica does a full resync from a live sibling instead of trusting
  its own now-stale log.
- **Failure demo**: `POST /v1/_colony/replicas/:id/fail` and `/heal` let you
  (or the website's playground) kill and revive a replica live and watch
  `GET /v1/_colony/stats` show the pheromone trail move to the survivor.
- **Rate limiting**: `server/rateLimit.js` is a per-identity token bucket.
  Every authenticated `/v1/*` request draws from a per-API-key bucket
  (`RATE_LIMIT_CAPACITY`, default 60 burst; `RATE_LIMIT_PER_SEC`, default 20
  refill/sec); `POST /v1/keys` (unauthenticated — it's the signup flow) draws
  from a much tighter per-IP bucket instead, since that's the one endpoint an
  abuser could hit to mint unlimited free identities. Both return `429` with
  a `Retry-After` header.
- **Key revocation**: `DELETE /v1/keys`, authenticated with the key being
  revoked. There's no admin account model — possession of the key is the
  only authorization there is, matching the no-signup-flow philosophy above.
  A revoked key immediately fails auth on every subsequent request.

## What's simulated vs. real

Real: the HTTP API, the storage (in-memory + WAL to disk), the routing
algorithm, the retries, the write-ahead durability, the multi-tenant
isolation. Simulated for determinism: each replica's latency is an
artificial `sleep()`, not real network hops — this is one process, not a
distributed cluster (see [`engine/src/colony_demo_main.cpp`](../engine/src/colony_demo_main.cpp)
for the real-multi-process-over-real-TCP version). If you want to see this
mechanism running as genuinely separate processes over real sockets with
real embedded SQL storage, that's what the C++ colony demo is for; this
service trades that for something a browser or `curl` can actually talk to.

## Not done yet (be honest about this before pointing anyone at it)

- No public deployment — this only runs on `localhost` right now. Deploying
  it for real needs a host that runs one persistent Node process with a
  writable, persistent disk mounted at `DATA_DIR` (Render, Fly.io, Railway,
  a VPS) — not a stateless serverless platform: the colony's replicas and
  WAL live in that one process's memory and local disk on purpose (that's
  what makes the failure/healing demo real instead of mocked), so a
  request-scoped serverless function would lose it between invocations.
- No persistence beyond a single machine's disk (no S3/replicated backups).
- No query language beyond "get by id" / "list whole collection" — no
  filtering, sorting, or pagination yet.
- The API key itself is the only secret; there's no per-key permission model.
