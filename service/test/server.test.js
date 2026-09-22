'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const PORT = 4281;
const BASE_URL = `http://localhost:${PORT}`;

let serverProcess;
let dataDir;

function waitForReady(proc) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('server did not start in time')), 10000);
    proc.stdout.on('data', (chunk) => {
      if (chunk.toString().includes('listening')) {
        clearTimeout(timeout);
        resolve();
      }
    });
    proc.on('exit', (code) => {
      if (code !== null && code !== 0) reject(new Error(`server exited early with code ${code}`));
    });
  });
}

test.before(async () => {
  dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'antsql-cloud-test-'));
  serverProcess = spawn(process.execPath, [path.join(__dirname, '..', 'server', 'index.js')], {
    env: { ...process.env, PORT: String(PORT), DATA_DIR: dataDir, KEY_CREATION_BURST: '100' },
    cwd: path.join(__dirname, '..'),
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await waitForReady(serverProcess);
});

test.after(() => {
  serverProcess.kill();
  fs.rmSync(dataDir, { recursive: true, force: true });
});

async function createKey() {
  const res = await fetch(`${BASE_URL}/v1/keys`, { method: 'POST' });
  assert.equal(res.status, 201);
  const { apiKey } = await res.json();
  assert.match(apiKey, /^ant_[0-9a-f]{48}$/);
  return apiKey;
}

function authed(apiKey, init = {}) {
  return { ...init, headers: { ...(init.headers || {}), Authorization: `Bearer ${apiKey}`, 'Content-Type': 'application/json' } };
}

test('rejects requests without a valid API key', async () => {
  const res = await fetch(`${BASE_URL}/v1/db/users`);
  assert.equal(res.status, 401);
});

test('create, read, patch, list, delete a document', async () => {
  const apiKey = await createKey();

  const created = await fetch(`${BASE_URL}/v1/db/users`, authed(apiKey, {
    method: 'POST',
    body: JSON.stringify({ name: 'Ada', balance: 100 }),
  }));
  assert.equal(created.status, 201);
  const doc = await created.json();
  assert.equal(doc.name, 'Ada');
  assert.ok(doc.id);

  const fetched = await fetch(`${BASE_URL}/v1/db/users/${doc.id}`, authed(apiKey));
  assert.equal(fetched.status, 200);
  const fetchedBody = await fetched.json();
  assert.equal(fetchedBody.name, 'Ada');
  assert.equal(fetchedBody.balance, 100);
  assert.ok(fetchedBody._meta.servedBy);

  const patched = await fetch(`${BASE_URL}/v1/db/users/${doc.id}`, authed(apiKey, {
    method: 'PATCH',
    body: JSON.stringify({ balance: 250 }),
  }));
  assert.equal(patched.status, 200);
  const patchedBody = await patched.json();
  assert.equal(patchedBody.name, 'Ada');
  assert.equal(patchedBody.balance, 250);

  const listed = await fetch(`${BASE_URL}/v1/db/users`, authed(apiKey));
  const { docs } = await listed.json();
  assert.equal(docs.length, 1);
  assert.equal(docs[0].balance, 250);

  const deleted = await fetch(`${BASE_URL}/v1/db/users/${doc.id}`, authed(apiKey, { method: 'DELETE' }));
  assert.equal(deleted.status, 200);

  const missing = await fetch(`${BASE_URL}/v1/db/users/${doc.id}`, authed(apiKey));
  assert.equal(missing.status, 404);
});

test('a revoked key can no longer authenticate', async () => {
  const apiKey = await createKey();

  const before = await fetch(`${BASE_URL}/v1/db/notes`, authed(apiKey));
  assert.equal(before.status, 200);

  const revoked = await fetch(`${BASE_URL}/v1/keys`, authed(apiKey, { method: 'DELETE' }));
  assert.equal(revoked.status, 200);
  assert.deepEqual(await revoked.json(), { revoked: true });

  const after = await fetch(`${BASE_URL}/v1/db/notes`, authed(apiKey));
  assert.equal(after.status, 401);
});

test('two API keys are isolated namespaces', async () => {
  const keyA = await createKey();
  const keyB = await createKey();

  const created = await fetch(`${BASE_URL}/v1/db/notes`, authed(keyA, {
    method: 'POST',
    body: JSON.stringify({ text: 'only for keyA' }),
  }));
  const doc = await created.json();

  const crossRead = await fetch(`${BASE_URL}/v1/db/notes/${doc.id}`, authed(keyB));
  assert.equal(crossRead.status, 404);
});

test('reads survive a replica failure and the colony reports it as unhealthy', async () => {
  const apiKey = await createKey();
  const created = await fetch(`${BASE_URL}/v1/db/accounts`, authed(apiKey, {
    method: 'POST',
    body: JSON.stringify({ owner: 'grace' }),
  }));
  const doc = await created.json();

  const before = await fetch(`${BASE_URL}/v1/db/accounts/${doc.id}`, authed(apiKey));
  const beforeBody = await before.json();
  const servingReplica = beforeBody._meta.servedBy;

  const failed = await fetch(`${BASE_URL}/v1/_colony/replicas/${servingReplica}/fail`, authed(apiKey, { method: 'POST' }));
  assert.equal(failed.status, 200);

  // The request must still succeed (self-healing at the request level) even
  // though the replica the colony previously preferred is now unreachable.
  const after = await fetch(`${BASE_URL}/v1/db/accounts/${doc.id}`, authed(apiKey));
  assert.equal(after.status, 200);
  const afterBody = await after.json();
  assert.equal(afterBody.owner, 'grace');
  assert.notEqual(afterBody._meta.servedBy, servingReplica);

  const stats = await fetch(`${BASE_URL}/v1/_colony/stats`, authed(apiKey));
  const statsBody = await stats.json();
  const replicaStat = statsBody.shards.flatMap((s) => s.replicas).find((r) => r.id === servingReplica);
  assert.equal(replicaStat.alive, false);

  const healed = await fetch(`${BASE_URL}/v1/_colony/replicas/${servingReplica}/heal`, authed(apiKey, { method: 'POST' }));
  assert.equal(healed.status, 200);
});

test('colony stats never expose another tenant\'s API key or data', async () => {
  const keyA = await createKey();
  const keyB = await createKey();
  const created = await fetch(`${BASE_URL}/v1/db/secrets`, authed(keyA, {
    method: 'POST',
    body: JSON.stringify({ v: 1 }),
  }));
  const doc = await created.json();
  await fetch(`${BASE_URL}/v1/db/secrets/${doc.id}`, authed(keyA));

  const statsB = await (await fetch(`${BASE_URL}/v1/_colony/stats`, authed(keyB))).json();
  const raw = JSON.stringify(statsB);
  assert.ok(!raw.includes(keyA), 'stats leaked another tenant\'s key');
  assert.equal(statsB.documents, 0);
  assert.deepEqual(statsB.pheromone, {});

  const statsA = await (await fetch(`${BASE_URL}/v1/_colony/stats`, authed(keyA))).json();
  assert.ok(!JSON.stringify(statsA).includes(keyA), 'stats echoed the caller\'s own key');
  assert.equal(statsA.documents, 1);
  assert.ok(Object.keys(statsA.pheromone).some((k) => k.startsWith('secrets#shard')));
});

test('failing a replica only affects the tenant that failed it, and heal resyncs missed writes', async () => {
  const keyA = await createKey();
  const keyB = await createKey();
  const statsBefore = await (await fetch(`${BASE_URL}/v1/_colony/stats`, authed(keyA))).json();
  const replicaIds = statsBefore.shards[0].replicas.map((r) => r.id);

  for (const id of replicaIds) {
    await fetch(`${BASE_URL}/v1/_colony/replicas/${id}/fail`, authed(keyA, { method: 'POST' }));
  }
  const statsB = await (await fetch(`${BASE_URL}/v1/_colony/stats`, authed(keyB))).json();
  assert.ok(statsB.shards[0].replicas.every((r) => r.alive), 'tenant B saw tenant A\'s injected failure');

  for (const id of replicaIds.slice(1)) {
    await fetch(`${BASE_URL}/v1/_colony/replicas/${id}/heal`, authed(keyA, { method: 'POST' }));
  }
  // Write while replicaIds[0] is still down for A, then heal it: it must
  // pick the write up from a sibling.
  const docs = [];
  for (let i = 0; i < 8; i++) {
    docs.push(await (await fetch(`${BASE_URL}/v1/db/items`, authed(keyA, { method: 'POST', body: JSON.stringify({ i }) }))).json());
  }
  await fetch(`${BASE_URL}/v1/_colony/replicas/${replicaIds[0]}/heal`, authed(keyA, { method: 'POST' }));
  const after = await (await fetch(`${BASE_URL}/v1/_colony/stats`, authed(keyA))).json();
  const shard0 = after.shards[0].replicas;
  assert.ok(shard0.every((r) => r.alive));
  assert.equal(new Set(shard0.map((r) => r.docCount)).size, 1, 'healed replica did not resync');
});

test('rejects oversized bodies, non-object bodies, and bad names', async () => {
  const apiKey = await createKey();
  const big = await fetch(`${BASE_URL}/v1/db/blobs`, authed(apiKey, { method: 'POST', body: JSON.stringify({ x: 'a'.repeat(300 * 1024) }) }));
  assert.equal(big.status, 413);
  const arr = await fetch(`${BASE_URL}/v1/db/blobs`, authed(apiKey, { method: 'POST', body: '[1,2]' }));
  assert.equal(arr.status, 400);
  const bad = await fetch(`${BASE_URL}/v1/db/bad.name`, authed(apiKey));
  assert.equal(bad.status, 400);
});

test('health check responds without auth', async () => {
  const res = await fetch(`${BASE_URL}/healthz`);
  assert.equal(res.status, 200);
});
