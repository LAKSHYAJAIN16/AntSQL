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

  async read(key) {
    if (!this.alive) throw new Error(`replica ${this.id} is unreachable`);
    await sleep(this.baseLatencyMs + Math.random() * 1.5);
    return this.store.has(key) ? this.store.get(key) : null;
  }

  // Used when a failed replica comes back: it missed every write while
  // down, so it needs a full copy from a live sibling rather than relying
  // on its own (now-stale) WAL.
  syncFrom(other) {
    this.store = new Map(other.store);
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
  constructor(dataDir) {
    this.dataDir = dataDir;
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
        this._indexFor(collectionKey).add(id);
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

  _shardFor(docKey) {
    return this.shards[hashToShard(docKey)];
  }

  async write(namespace, collection, id, value) {
    const collectionKey = `${namespace}/${collection}`;
    const docKey = `${collectionKey}/${id}`;
    const shard = this._shardFor(docKey);
    await Promise.all(shard.replicas.filter((r) => r.alive).map((r) => r.write(docKey, value)));
    if (value === null) this._indexFor(collectionKey).delete(id);
    else this._indexFor(collectionKey).add(id);
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
        const value = await replica.read(docKey);
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

  failReplica(replicaId) {
    for (const shard of this.shards) {
      const replica = shard.replicas.find((r) => r.id === replicaId);
      if (replica) {
        replica.alive = false;
        return true;
      }
    }
    return false;
  }

  healReplica(replicaId) {
    for (const shard of this.shards) {
      const replica = shard.replicas.find((r) => r.id === replicaId);
      if (replica) {
        replica.alive = true;
        const source = shard.replicas.find((r) => r.alive && r.id !== replicaId);
        if (source) replica.syncFrom(source);
        return true;
      }
    }
    return false;
  }

  stats() {
    const pheromone = {};
    for (const [routeKey, scores] of this.router.pheromone) {
      pheromone[routeKey] = Object.fromEntries(scores);
    }
    return {
      shards: this.shards.map((shard) => ({
        id: shard.id,
        replicas: shard.replicas.map((r) => ({
          id: r.id,
          alive: r.alive,
          baseLatencyMs: r.baseLatencyMs,
          docCount: r.store.size,
        })),
      })),
      pheromone,
    };
  }
}

module.exports = { Colony, SHARD_COUNT, hashToShard };
