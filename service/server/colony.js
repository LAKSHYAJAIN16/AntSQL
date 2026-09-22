'use strict';

const fs = require('fs');
const path = require('path');
const { Router } = require('./router');

const SHARD_COUNT = 4;
// Three replicas per shard with deliberately different simulated latency,
// so the colony has something real to learn (a uniform-cost ring just
// symmetry-breaks — see engine/src/colony_demo_main.cpp's own note on this).
const REPLICA_PROFILES = [
  { suffix: 'a', baseLatencyMs: 2 },
  { suffix: 'b', baseLatencyMs: 11 },
  { suffix: 'c', baseLatencyMs: 5 },
];

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function hashToShard(key) {
  let hash = 0;
  for (let i = 0; i < key.length; i++) hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
  return hash % SHARD_COUNT;
}

class Replica {
  constructor(id, baseLatencyMs, walPath) {
    this.id = id;
    this.baseLatencyMs = baseLatencyMs;
    this.alive = true;
    // Fault injection is scoped per tenant namespace: on a shared hosted
    // instance, one tenant "failing" a replica for the demo must not take
    // it down for everyone else.
    this.failedFor = new Set();
    this.store = new Map();
    this.walPath = walPath;
    this._replayWal();
  }

  _replayWal() {
    if (!fs.existsSync(this.walPath)) return;
    const lines = fs.readFileSync(this.walPath, 'utf8').split('\n').filter(Boolean);
    for (const line of lines) {
      const record = JSON.parse(line);
      if (record.op === 'set') this.store.set(record.key, record.value);
      else if (record.op === 'delete') this.store.delete(record.key);
    }
    // Compact on startup once the log is mostly overwritten history, so a
    // long-running hosted instance's WAL doesn't grow without bound.
    if (lines.length > 1000 && lines.length > 2 * this.store.size) this._compact();
  }

  _compact() {
    const tmp = `${this.walPath}.tmp`;
    const lines = [...this.store].map(([key, value]) => JSON.stringify({ op: 'set', key, value }));
    fs.writeFileSync(tmp, lines.length ? lines.join('\n') + '\n' : '');
    fs.renameSync(tmp, this.walPath);
  }

  isUpFor(namespace) {
    return this.alive && !this.failedFor.has(namespace);
  }

  _appendWal(record) {
    fs.appendFileSync(this.walPath, JSON.stringify(record) + '\n');
  }

  async write(key, value) {
    await sleep(this.baseLatencyMs + Math.random() * 1.5);
    if (value === null) {
      this.store.delete(key);
      this._appendWal({ op: 'delete', key });
    } else {
      this.store.set(key, value);
      this._appendWal({ op: 'set', key, value });
    }
  }

  async read(key, namespace) {
    if (!this.isUpFor(namespace)) throw new Error(`replica ${this.id} is unreachable`);
    await sleep(this.baseLatencyMs + Math.random() * 1.5);
    return this.store.has(key) ? this.store.get(key) : null;
  }

  // Used when a failed replica comes back for a namespace: it missed every
  // write to that namespace while down, so it copies that namespace's keys
  // from a live sibling. Changes go through the WAL too, so the resync
  // survives a restart instead of replaying the stale pre-failure state.
  syncPrefixFrom(other, prefix) {
    for (const key of [...this.store.keys()]) {
      if (key.startsWith(prefix) && !other.store.has(key)) {
        this.store.delete(key);
        this._appendWal({ op: 'delete', key });
      }
    }
    for (const [key, value] of other.store) {
      if (!key.startsWith(prefix)) continue;
      if (JSON.stringify(this.store.get(key)) === JSON.stringify(value)) continue;
      this.store.set(key, value);
      this._appendWal({ op: 'set', key, value });
    }
  }
}

class Shard {
  constructor(shardId, dataDir) {
    this.id = shardId;
    this.replicas = REPLICA_PROFILES.map(
      (profile) =>
        new Replica(
          `shard${shardId}-${profile.suffix}`,
          profile.baseLatencyMs,
          path.join(dataDir, `shard${shardId}-${profile.suffix}.wal`)
        )
    );
  }
}

class Colony {
  constructor(dataDir, { maxDocsPerTenant = Infinity } = {}) {
    this.dataDir = dataDir;
    this.maxDocsPerTenant = maxDocsPerTenant;
    // namespace -> live document count, for the per-tenant quota.
    this.tenantDocCounts = new Map();
    fs.mkdirSync(dataDir, { recursive: true });
    this.shards = Array.from({ length: SHARD_COUNT }, (_, i) => new Shard(i, dataDir));
    this.router = new Router();
    // Which document ids exist per namespace/collection, rebuilt from
    // whatever the replicas already recovered from their own WALs.
    this.indexes = new Map();
    this._rebuildIndexes();
    this._evaporationTimer = setInterval(() => this.router.evaporate(), 5000);
    this._evaporationTimer.unref();
  }

  _rebuildIndexes() {
    for (const shard of this.shards) {
      const representative = shard.replicas[0];
      for (const docKey of representative.store.keys()) {
        const collectionKey = docKey.slice(0, docKey.lastIndexOf('/'));
        const id = docKey.slice(docKey.lastIndexOf('/') + 1);
        this._addToIndex(collectionKey, id);
      }
    }
  }

  _indexFor(collectionKey) {
    let set = this.indexes.get(collectionKey);
    if (!set) {
      set = new Set();
      this.indexes.set(collectionKey, set);
    }
    return set;
  }

  _addToIndex(collectionKey, id) {
    const index = this._indexFor(collectionKey);
    if (index.has(id)) return;
    index.add(id);
    const ns = namespaceOf(collectionKey);
    this.tenantDocCounts.set(ns, (this.tenantDocCounts.get(ns) || 0) + 1);
  }

  _removeFromIndex(collectionKey, id) {
    if (!this._indexFor(collectionKey).delete(id)) return;
    const ns = namespaceOf(collectionKey);
    this.tenantDocCounts.set(ns, this.tenantDocCounts.get(ns) - 1);
  }

  _findReplica(replicaId) {
    for (const shard of this.shards) {
      const replica = shard.replicas.find((r) => r.id === replicaId);
      if (replica) return { shard, replica };
    }
    return null;
  }

  _shardFor(docKey) {
    return this.shards[hashToShard(docKey)];
  }

  async write(namespace, collection, id, value) {
    const collectionKey = `${namespace}/${collection}`;
    const docKey = `${collectionKey}/${id}`;
    const shard = this._shardFor(docKey);
    const isNew = value !== null && !this._indexFor(collectionKey).has(id);
    if (isNew && (this.tenantDocCounts.get(namespace) || 0) >= this.maxDocsPerTenant) {
      const error = new Error(`document quota reached (${this.maxDocsPerTenant} per API key)`);
      error.code = 'QUOTA_EXCEEDED';
      throw error;
    }
    await Promise.all(shard.replicas.filter((r) => r.isUpFor(namespace)).map((r) => r.write(docKey, value)));
    if (value === null) this._removeFromIndex(collectionKey, id);
    else this._addToIndex(collectionKey, id);
    return { id, shard: shard.id };
  }

  async delete(namespace, collection, id) {
    return this.write(namespace, collection, id, null);
  }

  async read(namespace, collection, id) {
    const collectionKey = `${namespace}/${collection}`;
    const docKey = `${collectionKey}/${id}`;
    const shard = this._shardFor(docKey);
    const routeKey = `${collectionKey}#shard${shard.id}`;
    const visited = new Set();
    let lastError = null;

    // Retries within one request try every remaining replica, but each
    // attempt is scored and learned from independently — a real client
    // never sees a stale/dead replica's failure as long as one replica in
    // the shard is reachable, while the colony still learns which replica
    // to prefer *first* next time.
    for (let attempt = 0; attempt < shard.replicas.length; attempt++) {
      const candidates = shard.replicas.map((r) => ({ id: r.id, linkLatencyMs: 1.0 }));
      const decision = this.router.choose(routeKey, candidates, visited);
      if (!decision) break;
      visited.add(decision.nextHop);
      const replica = shard.replicas.find((r) => r.id === decision.nextHop);
      const start = Date.now();
      try {
        const value = await replica.read(docKey, namespace);
        const elapsedMs = Date.now() - start;
        this.router.observeSuccess(routeKey, decision.nextHop, elapsedMs);
        return { value, servedBy: replica.id, shard: shard.id, elapsedMs, exists: value !== null };
      } catch (err) {
        this.router.observeFailure(routeKey, decision.nextHop);
        lastError = err;
      }
    }
    const error = new Error(`shard ${shard.id} has no reachable replica: ${lastError?.message}`);
    error.code = 'SHARD_UNAVAILABLE';
    throw error;
  }

  async list(namespace, collection) {
    const collectionKey = `${namespace}/${collection}`;
    const ids = [...this._indexFor(collectionKey)];
    const results = await Promise.all(
      ids.map(async (id) => {
        try {
          const { value } = await this.read(namespace, collection, id);
          return value === null ? null : { id, ...value };
        } catch {
          return null;
        }
      })
    );
    return results.filter((doc) => doc !== null);
  }

  failReplica(namespace, replicaId) {
    const found = this._findReplica(replicaId);
    if (!found) return false;
    found.replica.failedFor.add(namespace);
    return true;
  }

  healReplica(namespace, replicaId) {
    const found = this._findReplica(replicaId);
    if (!found) return false;
    const { shard, replica } = found;
    replica.failedFor.delete(namespace);
    const source = shard.replicas.find((r) => r.id !== replicaId && r.isUpFor(namespace));
    if (source) replica.syncPrefixFrom(source, `${namespace}/`);
    return true;
  }

  // Everything here is scoped to one tenant: replica health as that tenant
  // sees it, that tenant's doc counts, and only that tenant's pheromone
  // trails (with the namespace stripped from each route key, since the
  // namespace is the tenant's API key).
  stats(namespace) {
    const prefix = `${namespace}/`;
    const docCounts = new Map();
    for (const [collectionKey, ids] of this.indexes) {
      if (!collectionKey.startsWith(prefix)) continue;
      for (const id of ids) {
        const docKey = `${collectionKey}/${id}`;
        for (const r of this._shardFor(docKey).replicas) {
          if (r.store.has(docKey)) docCounts.set(r.id, (docCounts.get(r.id) || 0) + 1);
        }
      }
    }
    const pheromone = {};
    for (const [routeKey, scores] of this.router.pheromone) {
      if (routeKey.startsWith(prefix)) pheromone[routeKey.slice(prefix.length)] = Object.fromEntries(scores);
    }
    return {
      shards: this.shards.map((shard) => ({
        id: shard.id,
        replicas: shard.replicas.map((r) => ({
          id: r.id,
          alive: r.isUpFor(namespace),
          baseLatencyMs: r.baseLatencyMs,
          docCount: docCounts.get(r.id) || 0,
        })),
      })),
      pheromone,
      documents: this.tenantDocCounts.get(namespace) || 0,
      quota: Number.isFinite(this.maxDocsPerTenant) ? this.maxDocsPerTenant : null,
    };
  }
}

function namespaceOf(collectionKey) {
  return collectionKey.slice(0, collectionKey.indexOf('/'));
}

module.exports = { Colony, SHARD_COUNT, hashToShard };
