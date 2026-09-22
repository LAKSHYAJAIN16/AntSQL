'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { parseQuery, applyQuery } = require('../server/query');

const DOCS = [
  { id: 'a', name: 'Ada', age: 36, city: 'London', tags: { vip: true } },
  { id: 'b', name: 'Grace', age: 85, city: 'NYC' },
  { id: 'c', name: 'Linus', age: 21, city: 'Helsinki' },
  { id: 'd', name: 'Barbara', age: 21, city: 'NYC', tags: { vip: false } },
  { id: 'e', name: 'Ken', age: 'unknown' },
];

function run(qs) {
  return applyQuery(DOCS, parseQuery(new URLSearchParams(qs)));
}

test('filters with ==, range operators, and dotted paths', () => {
  assert.deepEqual(run('where=city==NYC').docs.map((d) => d.id), ['b', 'd']);
  assert.deepEqual(run('where=age>=36').docs.map((d) => d.id), ['a', 'b']);
  assert.deepEqual(run('where=age<36&where=city==NYC').docs.map((d) => d.id), ['d']);
  assert.deepEqual(run('where=tags.vip==true').docs.map((d) => d.id), ['a']);
  // A string field is never "greater than" a number.
  assert.ok(!run('where=age>0').docs.some((d) => d.id === 'e'));
  // Quoted JSON forces a string match.
  assert.deepEqual(run('where=age=="unknown"').docs.map((d) => d.id), ['e']);
});

test('orders by a field with id as the tiebreaker, both directions', () => {
  assert.deepEqual(run('where=age>0&orderBy=age').docs.map((d) => d.id), ['c', 'd', 'a', 'b']);
  assert.deepEqual(run('where=age>0&orderBy=age&dir=desc').docs.map((d) => d.id), ['b', 'a', 'd', 'c']);
});

test('paginates with limit and a startAfter cursor until exhausted', () => {
  const seen = [];
  let cursor = null;
  do {
    const page = run(`orderBy=name&limit=2${cursor ? `&startAfter=${cursor}` : ''}`);
    seen.push(...page.docs.map((d) => d.name));
    cursor = page.nextCursor;
  } while (cursor);
  assert.deepEqual(seen, ['Ada', 'Barbara', 'Grace', 'Ken', 'Linus']);
});

test('rejects malformed queries', () => {
  assert.throws(() => run('where=nonsense'), /invalid where/);
  assert.throws(() => run('limit=0'), /limit/);
  assert.throws(() => run('dir=sideways'), /dir/);
  assert.throws(() => run('startAfter=zzz'), /cursor/);
});
