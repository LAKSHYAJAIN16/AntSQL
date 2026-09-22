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

const MAX_BODY_BYTES = Number(process.env.MAX_BODY_BYTES) || 256 * 1024;
// Behind a hosting proxy (Fly, Railway, Render) every request's socket
// address is the proxy's, so per-IP limits would lump all users together.
// Only trust forwarding headers when told to — otherwise any client could
// spoof them to dodge the key-creation limit.
const TRUST_PROXY = process.env.TRUST_PROXY === '1';
// Collection names and document ids become WAL keys and route keys, so
// keep them to a boring, unambiguous alphabet.
const NAME_PATTERN = /^[A-Za-z0-9_-]{1,128}$/;

const colony = new Colony(path.join(DATA_DIR, 'colony'), {
  maxDocsPerTenant: Number(process.env.MAX_DOCS_PER_KEY) || Infinity,
});
const keys = new KeyStore(path.join(DATA_DIR, 'keys.json'));

// Per-API-key limit: generous burst, sustained cap well above what the
// playground/demo scripts need. Per-IP limit on key creation is much
// tighter — that endpoint is unauthenticated, so it's the one an abuser
// would hit to mint keys for free.
const REQUEST_LIMIT = new RateLimiter({
  capacity: Number(process.env.RATE_LIMIT_CAPACITY) || 60,
  refillPerSec: Number(process.env.RATE_LIMIT_PER_SEC) || 20,
});
const KEY_CREATION_BURST = Number(process.env.KEY_CREATION_BURST) || 5;
const KEY_CREATION_LIMIT = new RateLimiter({ capacity: KEY_CREATION_BURST, refillPerSec: KEY_CREATION_BURST / 60 });
setInterval(() => {
  REQUEST_LIMIT.sweep();
  KEY_CREATION_LIMIT.sweep();
}, 60 * 1000).unref();

function clientIp(req) {
  if (TRUST_PROXY) {
    const forwarded = req.headers['fly-client-ip'] || (req.headers['x-forwarded-for'] || '').split(',')[0].trim();
    if (forwarded) return forwarded;
  }
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
    let size = 0;
    let tooLarge = false;
    // Past the limit, stop buffering but keep draining the upload, so the
    // client still receives the 413 instead of a reset connection.
    req.on('data', (chunk) => {
      size += chunk.length;
      if (tooLarge) return;
      if (size > MAX_BODY_BYTES) {
        tooLarge = true;
        chunks.length = 0;
        const error = new Error(`request body larger than ${MAX_BODY_BYTES} bytes`);
        error.code = 'BODY_TOO_LARGE';
        return reject(error);
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      if (tooLarge) return;
      if (chunks.length === 0) return resolve(undefined);
      let parsed;
      try {
        parsed = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      } catch {
        parsed = undefined;
      }
      // Documents are JSON objects; a bare string or array would otherwise
      // get spread into a nonsense document.
      if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
        const error = new Error('body must be a JSON object');
        error.code = 'INVALID_BODY';
        return reject(error);
      }
      resolve(parsed);
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

  if (req.method === 'GET' && url.pathname === '/healthz') {
    return sendJson(res, 200, { ok: true });
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
        return sendJson(res, 200, colony.stats(apiKey));
      }
      // POST /v1/_colony/replicas/:id/fail | /heal  (demo/admin controls)
      if (req.method === 'POST' && segments[1] === '_colony' && segments[2] === 'replicas' && segments[4]) {
        const replicaId = segments[3];
        const action = segments[4];
        // Scoped to the caller's namespace: this simulates an outage as
        // *this* tenant experiences it, never for anyone else.
        const ok = action === 'fail' ? colony.failReplica(apiKey, replicaId) : action === 'heal' ? colony.healReplica(apiKey, replicaId) : false;
        if (!ok) return sendJson(res, 404, { error: `unknown replica or action: ${replicaId}/${action}` });
        return sendJson(res, 200, { replicaId, action, ok: true });
      }

      // /v1/db/:collection[/:id]
      if (segments[1] === 'db' && segments[2]) {
        const collection = segments[2];
        const id = segments[3];
        if (!NAME_PATTERN.test(collection) || (id !== undefined && !NAME_PATTERN.test(id)) || segments.length > 4) {
          return sendJson(res, 400, { error: 'collection names and ids must match [A-Za-z0-9_-]{1,128}' });
        }

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
      const status = {
        SHARD_UNAVAILABLE: 503,
        QUOTA_EXCEEDED: 403,
        BODY_TOO_LARGE: 413,
        INVALID_BODY: 400,
      }[err.code] || 500;
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

// Hosts send SIGTERM before replacing a machine. WAL appends are
// synchronous, so every acknowledged write is already on disk; just stop
// accepting new connections and exit.
process.on('SIGTERM', () => {
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 5000).unref();
});

module.exports = { server, colony, keys };
