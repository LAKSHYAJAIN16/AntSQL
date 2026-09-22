'use strict';

// Collection queries, Firestore-shaped but expressed as URL params so they
// work from curl as well as the SDK:
//
//   GET /v1/db/users?where=age>=21&where=city==NYC&orderBy=age&dir=desc&limit=20
//   GET /v1/db/users?...&startAfter=<nextCursor from the previous page>
//
// Values are JSON-parsed when they parse (21, true, null, "21") and taken
// as plain strings otherwise (NYC). Fields may be dotted paths (address.city).
// Filtering happens after the colony reads each document, so this is a
// convenience over a scan, not an index.

const MAX_LIMIT = 1000;
const OPERATORS = ['==', '!=', '>=', '<=', '>', '<'];
const FIELD_PATTERN = /^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)*$/;

class QueryError extends Error {
  constructor(message) {
    super(message);
    this.code = 'INVALID_QUERY';
  }
}

function parseValue(raw) {
  try {
    return JSON.parse(raw);
  } catch {
    return raw;
  }
}

function parseWhere(clause) {
  for (const op of OPERATORS) {
    const at = clause.indexOf(op);
    if (at <= 0) continue;
    const field = clause.slice(0, at);
    // A shorter operator can match inside a longer one ("age>=21" contains
    // ">"); OPERATORS lists the two-character ones first, so only accept
    // this split if the field is a clean name.
    if (!FIELD_PATTERN.test(field)) continue;
    return { field, op, value: parseValue(clause.slice(at + op.length)) };
  }
  throw new QueryError(`invalid where clause "${clause}" — expected field<op>value with op one of ${OPERATORS.join(' ')}`);
}

function parseQuery(searchParams) {
  const where = searchParams.getAll('where').map(parseWhere);

  const orderBy = searchParams.get('orderBy');
  if (orderBy !== null && !FIELD_PATTERN.test(orderBy)) throw new QueryError(`invalid orderBy field "${orderBy}"`);

  const dir = searchParams.get('dir') || 'asc';
  if (dir !== 'asc' && dir !== 'desc') throw new QueryError('dir must be asc or desc');

  const rawLimit = searchParams.get('limit');
  const limit = rawLimit === null ? MAX_LIMIT : Number(rawLimit);
  if (!Number.isInteger(limit) || limit < 1 || limit > MAX_LIMIT) throw new QueryError(`limit must be an integer from 1 to ${MAX_LIMIT}`);

  return { where, orderBy, dir, limit, startAfter: searchParams.get('startAfter') };
}

function getPath(doc, field) {
  let value = doc;
  for (const part of field.split('.')) {
    if (value === null || typeof value !== 'object') return undefined;
    value = value[part];
  }
  return value;
}

// Total order across JSON types so mixed-type fields still sort
// deterministically: missing < null < booleans < numbers < strings < other.
function typeRank(v) {
  if (v === undefined) return 0;
  if (v === null) return 1;
  if (typeof v === 'boolean') return 2;
  if (typeof v === 'number') return 3;
  if (typeof v === 'string') return 4;
  return 5;
}

function compare(a, b) {
  const ra = typeRank(a);
  const rb = typeRank(b);
  if (ra !== rb) return ra - rb;
  if (ra === 5) {
    const sa = JSON.stringify(a);
    const sb = JSON.stringify(b);
    return sa < sb ? -1 : sa > sb ? 1 : 0;
  }
  return a < b ? -1 : a > b ? 1 : 0;
}

function matches(doc, { field, op, value }) {
  const actual = getPath(doc, field);
  if (op === '==') return compare(actual, value) === 0;
  if (op === '!=') return compare(actual, value) !== 0;
  // Range comparisons only make sense within one type, the same rule
  // Firestore uses: {age: "old"} is not > 21.
  if (typeRank(actual) !== typeRank(value)) return false;
  const c = compare(actual, value);
  if (op === '>') return c > 0;
  if (op === '>=') return c >= 0;
  if (op === '<') return c < 0;
  return c <= 0;
}

// docs: [{ id, ...fields }]. Returns { docs, nextCursor }.
function applyQuery(docs, query) {
  const filtered = docs.filter((doc) => query.where.every((clause) => matches(doc, clause)));

  const sign = query.dir === 'desc' ? -1 : 1;
  // Ties (and the no-orderBy case) fall back to id, so pages are stable
  // and a cursor always points at one exact position.
  filtered.sort((a, b) => {
    const byField = query.orderBy ? compare(getPath(a, query.orderBy), getPath(b, query.orderBy)) : 0;
    return sign * (byField || compare(a.id, b.id));
  });

  let start = 0;
  if (query.startAfter !== null) {
    const at = filtered.findIndex((doc) => doc.id === query.startAfter);
    if (at === -1) throw new QueryError('startAfter cursor does not match a document in this result set');
    start = at + 1;
  }

  const page = filtered.slice(start, start + query.limit);
  const hasMore = start + query.limit < filtered.length;
  return { docs: page, nextCursor: hasMore ? page[page.length - 1].id : null };
}

module.exports = { parseQuery, applyQuery, QueryError, MAX_LIMIT };
