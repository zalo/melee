// node --test native/tools/soak_report_worker/test.mjs
// Runs the Worker against an in-memory SQLite that stands in for D1.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { test } from 'node:test';
import { gzipSync } from 'node:zlib';

import worker, { MAX_BODY, MAX_PER_CLIENT_PER_DAY } from './src/index.js';

const REPORT = 'melee soak report v1\nresult: completed 30 minutes\nversion: portmaster-20261001-189c8a1\n' +
  'device: RG351P cpu=rk3326\ncfw: AmberELEC\tx\x1b[31m\nbinary: 26d946a2e50d\nmatches: 41\n' +
  '== memory ==\n== log ==\nresult: not a header\n';

function environment() {
  const database = new DatabaseSync(':memory:');
  database.exec(readFileSync(new URL('./schema.sql', import.meta.url), 'utf8'));
  const prepare = (sql) => {
    let values = [];
    const statement = {
      bind(...bound) { values = bound.map((value) => (value instanceof ArrayBuffer ? new Uint8Array(value) : value)); return statement; },
      async first() { return database.prepare(sql).get(...values) ?? null; },
      async all() { return { results: database.prepare(sql).all(...values) }; },
      async run() { database.prepare(sql).run(...values); },
    };
    return statement;
  };
  return { DB: { prepare }, QUERY_TOKEN: 'secret-token', CLIENT_SALT: 'salt' };
}

const call = (env, path, options = {}) => worker.fetch(new Request(`https://reports.example${path}`, options), env);
const post = (env, body, address = '203.0.113.7') => call(env, '/report', {
  method: 'POST', body, headers: { 'Content-Length': String(body.length), 'CF-Connecting-IP': address },
});
const query = (env, path, token = 'secret-token') => call(env, path, { headers: { Authorization: `Bearer ${token}` } });

test('stores a report and indexes its header', async () => {
  const env = environment();
  const body = gzipSync(REPORT);
  const answer = await post(env, body);
  assert.equal(answer.status, 200);
  const id = (await answer.text()).match(/^ok ([0-9a-f]{8})\n$/)[1];

  const { reports } = await (await query(env, '/reports')).json();
  assert.equal(reports.length, 1);
  // Control characters in a header value never reach the index; lines after a section are ignored.
  assert.deepEqual({ ...reports[0], received: null, client: null }, {
    id, received: null, client: null, size: body.length, result: 'completed 30 minutes', date: null,
    version: 'portmaster-20261001-189c8a1', binary: '26d946a2e50d', modes: null, device: 'RG351P cpu=rk3326',
    cfw: 'AmberELEC?x?[31m', kernel: null, memtotal: null, matches: 41, last: null,
  });
  // The sender's address is kept only as a salted hash.
  assert.match(reports[0].client, /^[0-9a-f]{16}$/);
  const stored = await query(env, `/reports/${id}`);
  assert.deepEqual(Buffer.from(await stored.arrayBuffer()), body);
  assert.equal((await query(env, '/reports/00000000')).status, 404);
});

test('rejects what is not a report', async () => {
  const env = environment();
  for (const body of [Buffer.from('plain text'), gzipSync('something else\n')]) {
    assert.equal((await post(env, body)).status, 400);
  }
  assert.equal((await post(env, Buffer.alloc(MAX_BODY + 1))).status, 413);
  assert.equal((await call(env, '/report', { method: 'PUT', body: 'x' })).status, 405);
  assert.deepEqual((await (await query(env, '/reports')).json()).reports, []);
});

test('reading needs the token', async () => {
  const env = environment();
  await post(env, gzipSync(REPORT));
  for (const path of ['/reports', '/reports/00000000']) {
    assert.equal((await call(env, path)).status, 401);
    assert.equal((await query(env, path, 'wrong')).status, 401);
  }
  // Without a configured token nothing can be read at all.
  assert.equal((await query({ ...env, QUERY_TOKEN: '' }, '/reports', '')).status, 401);
  assert.equal(await (await call(env, '/')).text(), 'melee soak report receiver\n');
});

test('filters the index', async () => {
  const env = environment();
  await post(env, gzipSync(REPORT));
  await post(env, gzipSync(REPORT.replace('RG351P', 'Miyoo Flip').replace('completed 30 minutes', 'CRASHED (exit status 139)')));
  const ids = async (search) => (await (await query(env, `/reports?${search}`)).json()).reports.map((report) => report.device);
  assert.deepEqual(await ids('device=flip'), ['Miyoo Flip cpu=rk3326']);
  assert.deepEqual(await ids('result=CRASHED'), ['Miyoo Flip cpu=rk3326']);
  assert.equal((await ids('version=portmaster-20261001-189c8a1')).length, 2);
  assert.equal((await ids('version=other')).length, 0);
  assert.equal((await ids('since=2999-01-01')).length, 0);
  assert.equal((await ids('limit=1')).length, 1);
});

test('limits one sender per day', async () => {
  const env = environment();
  const body = gzipSync(REPORT);
  for (let i = 0; i < MAX_PER_CLIENT_PER_DAY; i++) assert.equal((await post(env, body)).status, 200);
  assert.equal((await post(env, body)).status, 429);
  assert.equal((await post(env, body, '203.0.113.8')).status, 200);
});
