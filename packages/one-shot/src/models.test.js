import { test } from 'node:test';
import assert from 'node:assert/strict';
import { resolveModelArgs } from './models.js';
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
