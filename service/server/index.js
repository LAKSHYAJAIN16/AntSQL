'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { Colony } = require('./colony');
const { KeyStore } = require('./auth');
const { RateLimiter } = require('./rateLimit');

const PORT = process.env.PORT ? Number(process.env.PORT) : 4280;
const DATA_DIR = process.env.DATA_DIR ? path.resolve(process.env.DATA_DIR) : path.join(__dirname, '..', 'data');
const WEBSITE_DIR = path.join(__dirname, '..', 'website');

const colony = new Colony(path.join(DATA_DIR, 'colony'));
const keys = new KeyStore(path.join(DATA_DIR, 'keys.json'));

// Per-API-key limit: generous burst, sustained cap well above what the
// playground/demo scripts need. Per-IP limit on key creation is much
// tighter — that endpoint is unauthenticated, so it's the one an abuser
// would hit to mint keys for free.
const REQUEST_LIMIT = new RateLimiter({
  capacity: Number(process.env.RATE_LIMIT_CAPACITY) || 60,
  refillPerSec: Number(process.env.RATE_LIMIT_PER_SEC) || 20,
});
const KEY_CREATION_LIMIT = new RateLimiter({ capacity: 5, refillPerSec: 5 / 60 });
setInterval(() => {
  REQUEST_LIMIT.sweep();
  KEY_CREATION_LIMIT.sweep();
}, 60 * 1000).unref();

function clientIp(req) {
  return req.socket.remoteAddress || 'unknown';
}

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
};

function sendJson(res, status, body, extraHeaders = {}) {
  const payload = JSON.stringify(body);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Authorization, Content-Type',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, PATCH, DELETE, OPTIONS',
    ...extraHeaders,
  });
  res.end(payload);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => {
      if (chunks.length === 0) return resolve(undefined);
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch (err) {
        reject(new Error('invalid JSON body'));
      }
    });
    req.on('error', reject);
  });
}

function authenticate(req) {
  const header = req.headers['authorization'] || '';
  const match = header.match(/^Bearer (.+)$/);
  const key = match ? match[1] : null;
  return keys.isValid(key) ? key : null;
}

function serveStatic(req, res, urlPath) {
  const relative = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
  const resolved = path.normalize(path.join(WEBSITE_DIR, relative));
  if (!resolved.startsWith(WEBSITE_DIR)) {
    res.writeHead(403);
    return res.end('forbidden');
  }
  fs.readFile(resolved, (err, contents) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      return res.end('not found');
    }
    const ext = path.extname(resolved);
    res.writeHead(200, { 'Content-Type': CONTENT_TYPES[ext] || 'application/octet-stream' });
    res.end(contents);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const segments = url.pathname.split('/').filter(Boolean);

  if (req.method === 'OPTIONS') {
    return sendJson(res, 204, {});
  }

  // Signing up IS creating a key: no email, no account, no approval step.
  // Rate-limited by IP since it's the one v1 endpoint that isn't
  // authenticated — otherwise it's a free way to mint unlimited identities.
  if (req.method === 'POST' && url.pathname === '/v1/keys') {
    const { allowed, retryAfterMs } = KEY_CREATION_LIMIT.take(clientIp(req));
    if (!allowed) {
      return sendJson(res, 429, { error: 'too many keys created from this address, slow down' }, {
        'Retry-After': String(Math.ceil(retryAfterMs / 1000)),
      });
    }
    return sendJson(res, 201, { apiKey: keys.create() });
  }

  if (segments[0] === 'v1') {
    const apiKey = authenticate(req);
    if (!apiKey) return sendJson(res, 401, { error: 'missing or invalid Authorization: Bearer <apiKey>' });

    const { allowed, retryAfterMs } = REQUEST_LIMIT.take(apiKey);
    if (!allowed) {
      return sendJson(res, 429, { error: 'rate limit exceeded for this API key' }, {
        'Retry-After': String(Math.ceil(retryAfterMs / 1000)),
      });
    }

    // Revoking your own key: no admin model, so possession of the key IS
    // the authorization. After this the key can no longer authenticate.
    if (req.method === 'DELETE' && segments[1] === 'keys') {
      keys.revoke(apiKey);
      return sendJson(res, 200, { revoked: true });
    }

    try {
      // GET  /v1/_colony/stats
      if (req.method === 'GET' && segments[1] === '_colony' && segments[2] === 'stats') {
        return sendJson(res, 200, colony.stats());
      }
      // POST /v1/_colony/replicas/:id/fail | /heal  (demo/admin controls)
      if (req.method === 'POST' && segments[1] === '_colony' && segments[2] === 'replicas' && segments[4]) {
        const replicaId = segments[3];
        const action = segments[4];
        const ok = action === 'fail' ? colony.failReplica(replicaId) : action === 'heal' ? colony.healReplica(replicaId) : false;
        if (!ok) return sendJson(res, 404, { error: `unknown replica or action: ${replicaId}/${action}` });
        return sendJson(res, 200, { replicaId, action, ok: true });
      }

      // /v1/db/:collection[/:id]
      if (segments[1] === 'db' && segments[2]) {
        const collection = segments[2];
        const id = segments[3];

        if (req.method === 'POST' && !id) {
          const body = (await readBody(req)) || {};
          const newId = crypto.randomUUID();
          await colony.write(apiKey, collection, newId, body);
          return sendJson(res, 201, { id: newId, ...body });
        }
        if (req.method === 'GET' && !id) {
          const docs = await colony.list(apiKey, collection);
          return sendJson(res, 200, { docs });
        }
        if (req.method === 'PUT' && id) {
          const body = (await readBody(req)) || {};
          await colony.write(apiKey, collection, id, body);
          return sendJson(res, 200, { id, ...body });
        }
        if (req.method === 'PATCH' && id) {
          const existing = await colony.read(apiKey, collection, id);
          if (!existing.exists) return sendJson(res, 404, { error: 'not found' });
          const patch = (await readBody(req)) || {};
          const merged = { ...existing.value, ...patch };
          await colony.write(apiKey, collection, id, merged);
          return sendJson(res, 200, { id, ...merged });
        }
        if (req.method === 'GET' && id) {
          const result = await colony.read(apiKey, collection, id);
          if (!result.exists) return sendJson(res, 404, { error: 'not found' });
          return sendJson(res, 200, {
            id,
            ...result.value,
            _meta: { servedBy: result.servedBy, shard: result.shard, elapsedMs: result.elapsedMs },
          });
        }
        if (req.method === 'DELETE' && id) {
          await colony.delete(apiKey, collection, id);
          return sendJson(res, 200, { id, deleted: true });
        }
      }

      return sendJson(res, 404, { error: 'no such route' });
    } catch (err) {
      const status = err.code === 'SHARD_UNAVAILABLE' ? 503 : 500;
      return sendJson(res, status, { error: err.message });
    }
  }

  if (req.method === 'GET') return serveStatic(req, res, url.pathname);
  return sendJson(res, 404, { error: 'no such route' });
});

server.listen(PORT, () => {
  console.log(`AntSQL Cloud listening on http://localhost:${PORT}`);
  console.log(`  website:    http://localhost:${PORT}/`);
  console.log(`  create key: curl -X POST http://localhost:${PORT}/v1/keys`);
});

module.exports = { server, colony, keys };
