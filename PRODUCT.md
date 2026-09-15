# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

Indie and hackathon builders: developers who want a working backend in minutes for a side
project, demo, or prototype. Speed and "it just works" matter more than deep configurability.
They are evaluating this the way they'd evaluate Firebase/Firestore/Supabase for a weekend build,
not procuring production infrastructure.

## Product Purpose

AntSQL Cloud (`service/`) is a Firestore-simple document database: collections, documents, one
API key as the entire signup flow, no schema, no ops. It exists to prove that a genuinely
adaptive, self-healing routing algorithm (not a static load balancer) can sit under a product
simple enough for a solo developer to `curl` against in one command.

## Positioning

Every read is routed by a real pheromone-based ant-colony algorithm (`server/router.js`, a
line-for-line JS port of the C++ research engine's `router.hpp`) across replicated shards: it
learns which replica is fastest and reroutes around failures on its own, with no coordinator ever
recomputing a global plan. This is not a simulation bolted onto a demo — the HTTP API, storage
(in-memory + write-ahead log to disk), routing, retries, and multi-tenant isolation are all real
and independently testable (`npm test`, 9 passing tests including a live replica-failure/recovery
case). A competing product could copy the Firestore-simple API shape; it could not copy-paste the
adaptive routing behavior without the same pheromone/evaporation/reinforcement mechanism.

## Operating Context

- One API key = one isolated tenant namespace; creating a key over `POST /v1/keys` (unauthenticated)
  IS the signup flow.
- `GET/POST/PUT/PATCH/DELETE /v1/db/:collection[/:id]` is the whole document API today: get-by-id
  and list-whole-collection only, no filtering/sorting/pagination yet.
- `GET /v1/_colony/stats` plus `POST /v1/_colony/replicas/:id/fail` and `/heal` let a developer (or
  the site's own playground) kill and revive a replica live and watch the pheromone trail move to
  the survivor — this is the product's own proof mechanism, not a separate marketing demo.
- Runs today as a single long-lived Node process (4 shards × 3 replicas, deliberately different
  simulated per-replica latency) with WAL-to-disk durability at `DATA_DIR`; rate-limited per API
  key (token bucket) and per-IP on key creation; keys are self-service revocable
  (`DELETE /v1/keys`).
- The website IS the product surface for this audience: landing copy, quickstart, an interactive
  playground that talks to the live server (not faked), and the live colony/failure demo, at
  `service/website/index.html`.

## Capabilities and Constraints

- No public deployment yet (`localhost` only as of this writing) — deployment needs a host running
  one persistent process with a mounted persistent disk (Render/Fly.io/Railway/a VPS), not
  stateless serverless, because the colony's replicas and WAL intentionally live in one process's
  memory and local disk (that is what makes the failure/healing demo real instead of mocked).
- No query filtering, sorting, or pagination beyond get-by-id / list-whole-collection.
- No persistence beyond a single machine's disk (no S3/replicated backups) and no per-key
  permission model beyond the key itself.
- These are honest, stated limitations, not gaps to paper over in copy — the existing README
  explicitly says "be honest about this before pointing anyone at it," and that voice should
  survive into the product page.

## Brand Commitments

- Product name "AntSQL Cloud" is fixed.
- Everything else — palette, type, layout, the current orange accent, the ant-colony/pheromone
  terminology's visual treatment — is explicitly open to full replacement. The user asked for a
  Fauna-inspired redesign and confirmed the current visual identity is not load-bearing.

## Evidence on Hand

- Real, working API and SDK (`sdk/antsql-client.js`, zero-dependency, Firestore-shaped
  `db.collection(x).doc(y).get()/set()/update()/delete()`), a real passing test suite, and a real
  local server a visitor can `curl` or click through in the playground.
- No customers, testimonials, case studies, benchmarks, or pricing exist. None may be fabricated
  or implied by the redesign — the page must earn belief from the live playground/failure demo,
  not from invented social proof.

## Product Principles

1. Prove, don't just claim: the playground and live-colony fail/heal demo are the product's actual
   argument, not a decorative screenshot — the redesign must keep them prominent and functional,
   not bury them under marketing weight.
2. Speed to first working request is the product's core promise to this audience; nothing in the
   redesign should add friction between landing and a working `curl`/SDK call.
3. Technical credibility without fabricated authority: the product proves itself through live
   mechanism and honest disclosure of limitations, never through invented trust signals.
4. The underlying mechanism (adaptive, decentralized routing) is the one thing a competitor cannot
   copy-paste — the visual identity should make that mechanism legible, not decorate around it.

## Accessibility & Inclusion

No product-specific accessibility requirement has been established beyond standard web
accessibility (keyboard operability, focus visibility, sufficient contrast, accessible labels on
the playground's interactive controls).
