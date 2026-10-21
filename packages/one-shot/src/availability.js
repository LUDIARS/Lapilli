import { resolveModel } from './models.js';

const TIERS = Object.freeze({
  ultra: [{ provider: 'claude', role: 'opus' }, { provider: 'codex', role: 'astra' }],
  high: [{ provider: 'claude', role: 'sonnet' }, { provider: 'codex', role: 'sol' }],
  medium: [{ provider: 'claude', role: 'haiku' }, { provider: 'codex', role: 'luna' }],
  low: [{ provider: 'claude', role: 'haiku' }, { provider: 'codex', role: 'luna' }],
});

function remaining(observation, role, now, maxAgeMs) {
  if (!observation || !Number.isFinite(observation.observedAt)
    || observation.observedAt > now || now - observation.observedAt > maxAgeMs) return null;
  const generalWindows = observation.windows ?? [];
  const modelWindows = observation.modelWindows?.[role] ?? [];
  if (!Array.isArray(generalWindows) || !Array.isArray(modelWindows)) return null;
  const windows = [...generalWindows, ...modelWindows];
  if (windows.length === 0) return null;
  const amounts = windows.map(w => !w || !Number.isFinite(w.usedPercent)
    || w.usedPercent < 0 || w.usedPercent > 100 || !Number.isFinite(w.resetsAt)
    || w.resetsAt * 1000 <= now ? null : 100 - w.usedPercent);
  if (amounts.some(amount => amount === null || amount <= 0)) return null;
  return Math.min(...amounts);
}

/** Pure allocation. Times use observedAt/now milliseconds and resetsAt epoch seconds. */
export function selectAvailableModel(options) {
  const { requested, availability, tier = 'high', env, now = Date.now(), maxAgeMs = 300_000 } = options;
  if (requested) {
    if (!['claude', 'codex'].includes(requested.provider) || !requested.model) throw new Error('Invalid explicit model request');
    return { provider: requested.provider, model: resolveModel(requested.model, requested.provider, env), reason: 'explicit-model' };
  }
  if (!Object.hasOwn(TIERS, tier)) throw new Error('Invalid one-shot tier');
  if (!Number.isFinite(now) || !Number.isFinite(maxAgeMs) || maxAgeMs < 0) throw new Error('Invalid availability clock');
  const candidates = TIERS[tier].map(({ provider, role }) => {
    return { provider, role, remaining: remaining(availability?.[provider], role, now, maxAgeMs) };
  }).filter(c => c.remaining !== null).sort((a, b) => b.remaining - a.remaining);
  const chosen = candidates[0];
  if (!chosen) throw new Error(`one_shot_model_unavailable:${tier}`);
  return { provider: chosen.provider, model: resolveModel(chosen.role, chosen.provider, env),
    reason: `available:${tier}`, remainingPercent: chosen.remaining };
}
