// Model roles are resolved here, never by CLI aliases or consumer default pins.
// Snapshot of the active Concordia model catalog, 2026-10-03.
export const DEFAULT_MODELS = Object.freeze({
  opus: 'claude-opus-5-5', sonnet: 'claude-sonnet-5', haiku: 'claude-haiku-4-5-20251001',
  fable: 'claude-fable-5-1', astra: 'gpt-6-astra', sol: 'gpt-6-sol', luna: 'gpt-6-luna',
});

export function resolveModel(roleOrId, provider, env = process.env) {
  const requested = roleOrId || (provider === 'claude' ? 'sonnet' : 'sol');
  const model = Object.hasOwn(DEFAULT_MODELS, requested)
    ? env[`LUDIARS_ONESHOT_MODEL_${requested.toUpperCase()}`] || DEFAULT_MODELS[requested]
    : requested;
  if (!/^[a-zA-Z0-9][a-zA-Z0-9._:/-]*$/.test(model)) throw new Error('Invalid one-shot model');
  if (provider === 'claude' && !model.startsWith('claude-')) throw new Error('Claude requires a Claude model');
  if (provider === 'codex' && !model.startsWith('gpt-')) throw new Error('Codex requires a GPT model');
  return model;
}

export function resolveModelArgs(args, provider, env) {
  const result = [...args];
  const index = result.findIndex(arg => arg === '--model' || arg === '-m');
  const inline = result.findIndex(arg => arg.startsWith('--model='));
  if (index >= 0) {
    if (!result[index + 1]) throw new Error('Missing model argument');
    result[index + 1] = resolveModel(result[index + 1], provider, env);
  } else if (inline >= 0) {
    result[inline] = `--model=${resolveModel(result[inline].slice(8), provider, env)}`;
  } else {
    // Insert before the stdin sentinel rather than after a positional prompt.
    const position = provider === 'codex' ? result.indexOf('exec') + 1 : 0;
    result.splice(position, 0, '--model', resolveModel(undefined, provider, env));
  }
  return result;
}
