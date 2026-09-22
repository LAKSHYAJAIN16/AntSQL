'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
const { AntSQL } = require('../sdk/antsql-client');

const PORT = 4282;
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
  });
}

test.before(async () => {
  dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'antsql-cloud-sdk-test-'));
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

test('SDK reads like Firestore: add/get/update/delete', async () => {
  const apiKey = await AntSQL.createKey(BASE_URL);
  const db = new AntSQL({ apiKey, baseUrl: BASE_URL });

  const ref = await db.collection('users').add({ name: 'Ada', balance: 100 });
  assert.ok(ref.id);

  const snap = await db.collection('users').doc(ref.id).get();
  assert.equal(snap.exists, true);
  assert.deepEqual(snap.data(), { name: 'Ada', balance: 100 });

  await db.collection('users').doc(ref.id).update({ balance: 500 });
  const updated = await db.collection('users').doc(ref.id).get();
  assert.equal(updated.data().balance, 500);

  const all = await db.collection('users').list();
  assert.equal(all.length, 1);

  await db.collection('users').doc(ref.id).delete();
  const gone = await db.collection('users').doc(ref.id).get();
  assert.equal(gone.exists, false);
});

test('SDK surfaces the colony status for the dashboard', async () => {
  const apiKey = await AntSQL.createKey(BASE_URL);
  const db = new AntSQL({ apiKey, baseUrl: BASE_URL });
  const stats = await db.colonyStats();
  assert.ok(Array.isArray(stats.shards));
  assert.ok(stats.shards.length > 0);
});

test('SDK queries: where, orderBy, limit, and cursor pagination end to end', async () => {
  const apiKey = await AntSQL.createKey(BASE_URL);
  const db = new AntSQL({ apiKey, baseUrl: BASE_URL });
  const people = db.collection('people');
  for (const [name, age, city] of [['Ada', 36, 'London'], ['Grace', 85, 'New York'], ['Linus', 21, 'Helsinki'], ['Barbara', 21, 'New York']]) {
    await people.add({ name, age, city });
  }

  // A value with a space and a + sign must survive URL encoding.
  const nyc = await people.where('city', '==', 'New York').orderBy('age').get();
  assert.deepEqual(nyc.docs.map((d) => d.name), ['Barbara', 'Grace']);

  const adults = await people.where('age', '>', 21).orderBy('age', 'desc').get();
  assert.deepEqual(adults.docs.map((d) => d.name), ['Grace', 'Ada']);

  const byName = people.orderBy('name').limit(3);
  const first = await byName.get();
  assert.deepEqual(first.docs.map((d) => d.name), ['Ada', 'Barbara', 'Grace']);
  const second = await byName.startAfter(first.nextCursor).get();
  assert.deepEqual(second.docs.map((d) => d.name), ['Linus']);
  assert.equal(second.nextCursor, null);

  await assert.rejects(() => people.limit(0).get(), /limit/);
});
