import { test } from 'node:test';
import assert from 'node:assert/strict';
import { utimesSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { resolveModelArgs, resolveModel } from './models.js';
import { roleMapPath } from './role-map.js';
import { createRequire } from 'node:module';
import { subscriptionEnvironment } from './environment.js';
import { fixtureEnv, missingEnv } from './role-map-fixture.test-helper.js';

const env = fixtureEnv();

test('roles resolve through the role map while explicitly pinned models stay exact', () => {
  assert.deepEqual(resolveModelArgs(['-p', '--model', 'opus'], 'claude', env), ['-p', '--model', 'claude-opus-test']);
  assert.deepEqual(resolveModelArgs(['exec', '-m', 'gpt-6-luna', '-'], 'codex', env), ['exec', '-m', 'gpt-6-luna', '-']);
  assert.deepEqual(resolveModelArgs(['exec', '-'], 'codex', env), ['exec', '--model', 'gpt-test-sol', '-']);
  assert.deepEqual(resolveModelArgs(['-p'], 'claude', env), ['--model', 'claude-sonnet-test', '-p']);
  assert.throws(() => resolveModelArgs(['-p', '--model'], 'claude', env));
  assert.throws(() => resolveModelArgs(['-p', '--model', 'gpt-6-sol'], 'claude', env));
});

test('the role map follows file updates and env overrides win', () => {
  const local = fixtureEnv();
  assert.equal(resolveModel('astra', 'codex', local), 'gpt-test-astra');
  writeFileSync(local.LUDIARS_MODEL_ROLES_FILE, JSON.stringify({ schemaVersion: 1, roles: { codex: { astra: 'gpt-next-astra' } } }));
  const later = new Date(Date.now() + 5_000);
  utimesSync(local.LUDIARS_MODEL_ROLES_FILE, later, later);
  assert.equal(resolveModel('astra', 'codex', local), 'gpt-next-astra');
  assert.equal(resolveModel('astra', 'codex', { ...local, LUDIARS_ONESHOT_MODEL_ASTRA: 'gpt-pinned-astra' }), 'gpt-pinned-astra');
});

test('without a role map Claude falls back to CLI aliases and Codex refuses', () => {
  const none = missingEnv();
  assert.equal(resolveModel('opus[1m]', 'claude', none), 'opus[1m]');
  assert.equal(resolveModel(undefined, 'claude', none), 'sonnet');
  assert.throws(() => resolveModel('fable', 'claude', none), /model_role_unresolved:fable/);
  assert.throws(() => resolveModel(undefined, 'codex', none), /model_role_unresolved:sol/);
});

test('an invalid role map is treated as absent', () => {
  const broken = fixtureEnv();
  writeFileSync(broken.LUDIARS_MODEL_ROLES_FILE, '{not json');
  assert.equal(resolveModel('haiku', 'claude', broken), 'haiku');
});

test('role map location matches Concordia', () => {
  assert.equal(roleMapPath({ LUDIARS_MODEL_ROLES_FILE: 'x.json' }, 'win32', 'H'), 'x.json');
  assert.equal(roleMapPath({ LOCALAPPDATA: 'L' }, 'win32', 'H'), join('L', 'LUDIARS', 'model-roles.json'));
  assert.equal(roleMapPath({}, 'linux', 'H'), join('H', '.local', 'state', 'ludiars', 'model-roles.json'));
});

test('subscription child environment does not mutate the service or retain API billing selectors', () => {
  const source = { PATH: '/bin', CODEX_HOME: '/account', CODEX_API_KEY: 'test', ANTHROPIC_API_KEY: 'test', LICTOR_PORT: '123', CONCORDIA_HOOK: '1' };
  assert.deepEqual(subscriptionEnvironment(source), { PATH: '/bin', CODEX_HOME: '/account' });
  assert.equal(source.CODEX_API_KEY, 'test');
});

test('context suffixes and explicit Codex identifiers survive migration', () => {
  assert.equal(resolveModel('opus[1m]', 'claude', env), 'claude-opus-test[1m]');
  assert.equal(resolveModel('claude-opus-4-7[1m]', 'claude', env), 'claude-opus-4-7[1m]');
  assert.equal(resolveModel('codex-mini-latest', 'codex', env), 'codex-mini-latest');
  assert.equal(resolveModel('5.3-codex', 'codex', env), '5.3-codex');
  assert.throws(() => resolveModel('opus', 'codex', env));
  assert.throws(() => resolveModel('sol', 'claude', env));
  assert.throws(() => resolveModel('claude-opus-5-5[1m]; echo x', 'claude', env));
});

test('CommonJS consumers load the public package entry without invoking a CLI', () => {
  const require = createRequire(import.meta.url);
  const library = require('@ludiars/one-shot');
  assert.equal(library.resolveModel('sonnet', 'claude', env), 'claude-sonnet-test');
  assert.deepEqual(library.MODEL_ROLES.codex, ['astra', 'sol', 'luna']);
  assert.equal(typeof library.spawnOneShot, 'function');
});
