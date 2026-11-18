// Model roles resolve through Concordia's role map (role-map.js); no model id is pinned here.
import { readRoleMap, roleProvider } from './role-map.js';

export { MODEL_ROLES, readRoleMap, roleMapPath } from './role-map.js';

// Claude Code resolves these aliases itself, so they are a safe fallback without a pinned id.
const CLAUDE_CLI_ALIASES = new Set(['opus', 'sonnet', 'haiku']);

function roleModel(role, provider, env) {
  const override = env[`LUDIARS_ONESHOT_MODEL_${role.toUpperCase()}`];
  if (override) return { model: override, alias: false };
  const mapped = readRoleMap(env)?.[provider]?.[role];
  if (typeof mapped === 'string' && mapped) return { model: mapped, alias: false };
  if (provider === 'claude' && CLAUDE_CLI_ALIASES.has(role)) return { model: role, alias: true };
  throw new Error(`model_role_unresolved:${role}`);
}

export function resolveModel(roleOrId, provider, env = process.env) {
  const requested = roleOrId || (provider === 'claude' ? 'sonnet' : 'sol');
  const [, role, context = ''] = requested.match(/^(.+?)(\[\d+[km]\])?$/i) ?? [];
  const owner = roleProvider(role);
  if (owner && owner !== provider) throw new Error(provider === 'claude' ? 'Claude requires a Claude model' : 'Codex requires a Codex model');
  const resolved = owner ? roleModel(role, provider, env) : { model: requested, alias: false };
  const model = owner && !resolved.model.includes('[') ? resolved.model + context : resolved.model;
  if (!/^[a-zA-Z0-9][a-zA-Z0-9._:/-]*(?:\[\d+[km]\])?$/i.test(model)) throw new Error('Invalid one-shot model');
  if (provider === 'claude' && !resolved.alias && !model.startsWith('claude-')) throw new Error('Claude requires a Claude model');
  // Codex also accepts explicit identifiers such as codex-mini-latest. Do not
  // require every CLI-owned identifier to use the current GPT naming scheme.
  if (provider === 'codex' && model.startsWith('claude-')) throw new Error('Codex requires a Codex model');
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
