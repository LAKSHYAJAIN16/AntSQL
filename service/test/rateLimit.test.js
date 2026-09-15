'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const PORT = 4283;
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
  dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'antsql-cloud-ratelimit-test-'));
  serverProcess = spawn(process.execPath, [path.join(__dirname, '..', 'server', 'index.js')], {
    // Tiny bucket, effectively no refill during the test, so a handful of
    // requests reliably trips the limit without depending on timing.
    env: { ...process.env, PORT: String(PORT), DATA_DIR: dataDir, RATE_LIMIT_CAPACITY: '3', RATE_LIMIT_PER_SEC: '0.001' },
    cwd: path.join(__dirname, '..'),
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await waitForReady(serverProcess);
});

test.after(() => {
  serverProcess.kill();
  fs.rmSync(dataDir, { recursive: true, force: true });
});

test('per-key rate limit returns 429 with Retry-After once the bucket is empty', async () => {
  const created = await fetch(`${BASE_URL}/v1/keys`, { method: 'POST' });
  const { apiKey } = await created.json();
  const headers = { Authorization: `Bearer ${apiKey}` };

  const results = [];
  for (let i = 0; i < 5; i++) {
    const res = await fetch(`${BASE_URL}/v1/db/probe`, { headers });
    results.push(res.status);
    if (res.status === 429) {
      assert.ok(res.headers.get('retry-after'));
      break;
    }
  }
  assert.ok(results.includes(429), `expected a 429 among ${JSON.stringify(results)}`);
  assert.deepEqual(results.slice(0, 3), [200, 200, 200]);
});

test('per-IP rate limit on key creation returns 429', async () => {
  const results = [];
  for (let i = 0; i < 8; i++) {
    const res = await fetch(`${BASE_URL}/v1/keys`, { method: 'POST' });
    results.push(res.status);
    if (res.status === 429) break;
  }
  assert.ok(results.includes(429), `expected a 429 among ${JSON.stringify(results)}`);
});
