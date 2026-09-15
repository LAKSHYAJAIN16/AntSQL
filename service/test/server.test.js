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
    env: { ...process.env, PORT: String(PORT), DATA_DIR: dataDir },
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
