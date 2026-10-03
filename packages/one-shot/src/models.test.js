import { test } from 'node:test';
import assert from 'node:assert/strict';
import { resolveModelArgs, resolveModel } from './models.js';
import { createRequire } from 'node:module';
import { subscriptionEnvironment } from './environment.js';

test('role defaults are concrete while explicitly pinned models stay exact', () => {
  assert.deepEqual(resolveModelArgs(['-p', '--model', 'opus'], 'claude', {}), ['-p', '--model', 'claude-opus-5-5']);
  assert.deepEqual(resolveModelArgs(['exec', '-m', 'gpt-6-luna', '-'], 'codex', {}), ['exec', '-m', 'gpt-6-luna', '-']);
  assert.deepEqual(resolveModelArgs(['exec', '-'], 'codex', {}), ['exec', '--model', 'gpt-6-sol', '-']);
  assert.throws(() => resolveModelArgs(['-p', '--model'], 'claude', {}));
  assert.throws(() => resolveModelArgs(['-p', '--model', 'gpt-6-sol'], 'claude', {}));
});

test('subscription child environment does not mutate the service or retain API billing selectors', () => {
  const source = { PATH: '/bin', CODEX_HOME: '/account', CODEX_API_KEY: 'test', ANTHROPIC_API_KEY: 'test', LICTOR_PORT: '123', CONCORDIA_HOOK: '1' };
  assert.deepEqual(subscriptionEnvironment(source), { PATH: '/bin', CODEX_HOME: '/account' });
  assert.equal(source.CODEX_API_KEY, 'test');
});

test('context suffixes and explicit Codex identifiers survive migration', () => {
  assert.equal(resolveModel('opus[1m]', 'claude', {}), 'claude-opus-5-5[1m]');
  assert.equal(resolveModel('claude-opus-4-7[1m]', 'claude', {}), 'claude-opus-4-7[1m]');
  assert.equal(resolveModel('codex-mini-latest', 'codex', {}), 'codex-mini-latest');
  assert.equal(resolveModel('5.3-codex', 'codex', {}), '5.3-codex');
  assert.throws(() => resolveModel('opus', 'codex', {}));
  assert.throws(() => resolveModel('claude-opus-5-5[1m]; echo x', 'claude', {}));
});

test('CommonJS consumers load the public package entry without invoking a CLI', () => {
  const require = createRequire(import.meta.url);
  const library = require('@ludiars/one-shot');
  assert.equal(library.resolveModel('sonnet', 'claude', {}), 'claude-sonnet-5');
  assert.equal(typeof library.spawnOneShot, 'function');
});
