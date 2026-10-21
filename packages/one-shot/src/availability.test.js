import { test } from 'node:test';
import assert from 'node:assert/strict';
import { selectAvailableModel, runAvailableOneShot } from './index.js';
const now = 1_000_000;
const observation = used => ({ observedAt: now, windows: [{ usedPercent: used, resetsAt: 2000 }] });
const availability = { claude: observation(100), codex: observation(30) };

test('weekly-only available quota substitutes within each tier', () => {
  for (const [tier, model] of [['ultra', 'gpt-6-astra'], ['high', 'gpt-6-sol'], ['medium', 'gpt-6-luna'], ['low', 'gpt-6-luna']]) {
    assert.deepEqual(selectAvailableModel({ tier, availability, now, env: {} }), {
      provider: 'codex', model, reason: `available:${tier}`, remainingPercent: 70,
    });
  }
});
test('unknown, missing, exhausted, stale, future and expired windows cannot supply availability', () => {
  for (const codex of [undefined, observation(100), observation(null), observation(-1), observation(101),
    { ...observation(0), windows: [] }, { ...observation(0), observedAt: 0 },
    { ...observation(0), observedAt: now + 1 },
    { ...observation(0), windows: [{ usedPercent: 0, resetsAt: 999 }] },
    { ...observation(0), windows: [...observation(0).windows, { usedPercent: null, resetsAt: null }] }]) {
    assert.throws(() => selectAvailableModel({ availability: { ...availability, codex }, now }), /unavailable/);
  }
});
test('model-specific exhaustion applies and an explicit pin is not substituted', () => {
  const claude = { ...observation(0), modelWindows: { opus: [{ usedPercent: 100, resetsAt: 2000 }] } };
  assert.equal(selectAvailableModel({ tier: 'ultra', availability: { claude, codex: observation(30) }, now }).provider, 'codex');
  assert.equal(selectAvailableModel({ availability, requested: { provider: 'claude', model: 'claude-opus-5-5[1m]' } }).model, 'claude-opus-5-5[1m]');
});
test('model-specific windows can establish availability without a general window', () => {
  const codex = { observedAt: now, modelWindows: { sol: [{ usedPercent: 20, resetsAt: 2000 }] } };
  assert.equal(selectAvailableModel({ availability: { ...availability, codex }, now }).remainingPercent, 80);
  assert.throws(() => selectAvailableModel({ availability: {
    ...availability, codex: { observedAt: now, modelWindows: { sol: {} } },
  }, now }), /unavailable/);
});
test('selection and execution are visible and a failed result is never replayed', async () => {
  const calls = [];
  const response = await runAvailableOneShot('summarize', { now, readAvailability: async () => availability,
    executors: { claude: async () => { calls.push('claude'); }, codex: async (prompt, selected) => {
      calls.push(selected.provider); assert.equal(prompt, 'summarize'); return { ok: false, partial: 'result unknown' };
    } },
  });
  assert.deepEqual(calls, ['codex']); assert.equal(response.selection.provider, 'codex'); assert.equal(response.result.ok, false);
});
test('executor exceptions, unavailable quotas and cancellation never start a fallback', async () => {
  let calls = 0;
  const options = { now, readAvailability: async () => availability, executors: {
    claude: async () => { calls++; }, codex: async () => { calls++; throw new Error('lost response'); },
  } };
  await assert.rejects(runAvailableOneShot('text', options), /lost response/); assert.equal(calls, 1);
  await assert.rejects(runAvailableOneShot('text', { ...options, readAvailability: async () => ({}) }), /unavailable/);
  const controller = new AbortController();
  await assert.rejects(runAvailableOneShot('text', { ...options, signal: controller.signal,
    readAvailability: async () => { controller.abort(); return availability; } }), { name: 'AbortError' });
  assert.equal(calls, 1);
});
test('explicit pin skips quota reads and missing executor fails before execution', async () => {
  const options = { requested: { provider: 'claude', model: 'opus' }, readAvailability: async () => { throw new Error('unexpected read'); },
    executors: { claude: async () => 'answer' } };
  assert.equal((await runAvailableOneShot('text', options)).result, 'answer');
  await assert.rejects(runAvailableOneShot('text', { ...options, executors: {} }), /executor missing/);
});
