'use strict';

// AntSQL Cloud SDK — zero dependencies, works in the browser or Node
// (anything with global fetch). Deliberately shaped like Firestore's
// client API: db.collection(x).doc(y).get()/set()/update()/delete(),
// db.collection(x).add(), db.collection(x).list(), and queries:
// db.collection(x).where('age', '>=', 21).orderBy('age').limit(10).get().
//
//   const db = new AntSQL({ apiKey: 'ant_...' });
//   const ref = await db.collection('users').add({ name: 'Ada' });
//   const snap = await db.collection('users').doc(ref.id).get();
//   console.log(snap.data());

class DocRef {
  constructor(client, collection, id) {
    this._client = client;
    this.collection = collection;
    this.id = id;
  }

  async get() {
    const res = await this._client._request('GET', `/v1/db/${this.collection}/${this.id}`);
    if (res.status === 404) return { exists: false, id: this.id, data: () => undefined };
    const body = await this._client._json(res);
    const { id, _meta, ...data } = body;
    return { exists: true, id, _meta, data: () => data };
  }

  async set(data) {
    const res = await this._client._request('PUT', `/v1/db/${this.collection}/${this.id}`, data);
    return this._client._json(res);
  }

  async update(patch) {
    const res = await this._client._request('PATCH', `/v1/db/${this.collection}/${this.id}`, patch);
    return this._client._json(res);
  }

  async delete() {
    const res = await this._client._request('DELETE', `/v1/db/${this.collection}/${this.id}`);
    return this._client._json(res);
  }
}

// Immutable query builder: each method returns a new Query, so a base
// query can be reused and extended without surprising aliasing.
class Query {
  constructor(client, collection, spec = { where: [], orderBy: null, dir: 'asc', limit: null, startAfter: null }) {
    this._client = client;
    this.collection = collection;
    this._spec = spec;
  }

  _with(changes) {
    return new Query(this._client, this.collection, { ...this._spec, ...changes });
  }

  where(field, op, value) {
    return this._with({ where: [...this._spec.where, `${field}${op}${JSON.stringify(value)}`] });
  }

  orderBy(field, dir = 'asc') {
    return this._with({ orderBy: field, dir });
  }

  limit(n) {
    return this._with({ limit: n });
  }

  // Pass the nextCursor from a previous page (or a document id).
  startAfter(cursor) {
    return this._with({ startAfter: cursor });
  }

  // Returns { docs: [{ id, ...fields }], nextCursor } — nextCursor is null
  // on the last page, otherwise pass it to startAfter() for the next one.
  async get() {
    const params = new URLSearchParams();
    for (const clause of this._spec.where) params.append('where', clause);
    if (this._spec.orderBy) {
      params.set('orderBy', this._spec.orderBy);
      params.set('dir', this._spec.dir);
    }
    if (this._spec.limit !== null) params.set('limit', String(this._spec.limit));
    if (this._spec.startAfter !== null) params.set('startAfter', this._spec.startAfter);
    const qs = params.toString();
    const res = await this._client._request('GET', `/v1/db/${this.collection}${qs ? `?${qs}` : ''}`);
    return this._client._json(res);
  }
}

class CollectionRef extends Query {
  constructor(client, collection) {
    super(client, collection);
  }

  doc(id) {
    return new DocRef(this._client, this.collection, id);
  }

  async add(data) {
    const res = await this._client._request('POST', `/v1/db/${this.collection}`, data);
    const body = await this._client._json(res);
    return new DocRef(this._client, this.collection, body.id);
  }

  // Every document in the collection (up to the server's 1000-per-page
  // cap; use where/orderBy/limit/startAfter to page through more).
  async list() {
    const { docs } = await this.get();
    return docs;
  }
}

class AntSQL {
  constructor({ apiKey, baseUrl = 'http://localhost:4280' } = {}) {
    if (!apiKey) throw new Error('AntSQL requires an apiKey — create one with AntSQL.createKey(baseUrl)');
    this.apiKey = apiKey;
    this.baseUrl = baseUrl.replace(/\/$/, '');
  }

  collection(name) {
    return new CollectionRef(this, name);
  }

  async colonyStats() {
    const res = await this._request('GET', '/v1/_colony/stats');
    return this._json(res);
  }

  // Revokes this client's own API key. There are no admin accounts in this
  // model — possession of the key is the only authorization needed, and
  // after this call every request with it (including further ones from
  // this client) will get 401.
  async revokeKey() {
    const res = await this._request('DELETE', '/v1/keys');
    return this._json(res);
  }

  async _request(method, path, body) {
    return fetch(`${this.baseUrl}${path}`, {
      method,
      headers: {
        Authorization: `Bearer ${this.apiKey}`,
        ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
      },
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  }

  async _json(res) {
    const body = await res.json();
    if (!res.ok) throw new Error(body.error || `request failed with status ${res.status}`);
    return body;
  }

  // Static helper: no signup form, just ask the server for a key.
  static async createKey(baseUrl = 'http://localhost:4280') {
    const res = await fetch(`${baseUrl.replace(/\/$/, '')}/v1/keys`, { method: 'POST' });
    const { apiKey } = await res.json();
    return apiKey;
  }
}

if (typeof module !== 'undefined' && module.exports) module.exports = { AntSQL };
if (typeof window !== 'undefined') window.AntSQL = AntSQL;
