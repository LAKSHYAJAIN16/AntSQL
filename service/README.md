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
const ref = await db.collection('users').add({ name: 'Ada', age: 36 });
console.log((await db.collection('users').doc(ref.id).get()).data());

// Queries: filters, ordering, and cursor pagination.
const page = await db.collection('users').where('age', '>=', 21).orderBy('age', 'desc').limit(20).get();
const next = await db.collection('users').where('age', '>=', 21).orderBy('age', 'desc').limit(20).startAfter(page.nextCursor).get();
```

The same query over HTTP (URL-encode the values):

```bash
curl -G http://localhost:4280/v1/db/users -H "Authorization: Bearer ant_..."   --data-urlencode 'where=age>=21' --data-urlencode 'orderBy=age' --data-urlencode 'dir=desc' --data-urlencode 'limit=20'
# {"docs":[...],"nextCursor":"<id>" | null}
```

Operators: `== != < <= > >=`. Values are JSON-parsed (`21`, `true`,
`"21"`), otherwise taken as strings. Fields can be dotted paths
(`address.city`). Range operators only match values of the same type.

In a browser, load the SDK straight from the server; it defaults to that
origin:

```html
<script src="https://<your-host>/sdk/antsql-client.js"></script>
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
  Both are **scoped to your API key**: a failure you inject only affects
  your reads, and stats only show your own documents and pheromone trails,
  so one tenant can't take down or observe another on a shared host.
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

## Deploy (Fly.io)

```bash
cd service
fly auth login
fly launch --no-deploy --copy-config   # picks a unique app name
fly volumes create antsql_data --size 1 --region iad
fly deploy
```

`fly.toml` scales to zero when idle (cheapest; ~1-2 s cold start) and
mounts the volume at `/data`. The `Dockerfile` also works on Railway or
Render, as long as a persistent volume is mounted at `DATA_DIR`. Don't use
a stateless serverless platform: the replicas and WAL live in one
process's memory and disk on purpose.

## Configuration

| Env var | Default | Meaning |
|---|---|---|
| `PORT` | `4280` | Listen port |
| `DATA_DIR` | `service/data` | WALs and key store; must be persistent |
| `TRUST_PROXY` | off | `1` to take the client IP from `Fly-Client-IP` / `X-Forwarded-For` (only behind a proxy) |
| `MAX_DOCS_PER_KEY` | unlimited | Per-tenant document quota (`403` when reached) |
| `MAX_BODY_BYTES` | `262144` | Request body cap (`413` above it) |
| `RATE_LIMIT_CAPACITY` / `RATE_LIMIT_PER_SEC` | `60` / `20` | Per-key token bucket |
| `KEY_CREATION_BURST` | `5` | Keys per IP per minute |

## Not done yet (be honest about this before pointing anyone at it)

- Replicas are simulated inside one process, so a machine crash takes all
  of them down at once. Real multi-machine replicas are the next step
  (see [`../SCOPE.md`](../SCOPE.md)).
- No backups beyond the host's volume snapshots.
- Queries scan the collection; there are no secondary indexes yet.
- No transactions or batched writes.
- The API key itself is the only secret; there's no per-key permission model.
