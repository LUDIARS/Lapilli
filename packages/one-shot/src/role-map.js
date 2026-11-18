// Role map written by Concordia (spec: Concordia spec/feature/model-role-map.md, CC-MODEL-ROLE-MAP).
// Model ids are never pinned in this package: roles resolve through this file.
import { readFileSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

export const MODEL_ROLES = Object.freeze({
  claude: Object.freeze(['opus', 'sonnet', 'haiku', 'fable']),
  codex: Object.freeze(['astra', 'sol', 'luna']),
});

/** Same rule as Concordia's writer: env > %LOCALAPPDATA%\LUDIARS > $XDG_STATE_HOME/ludiars. */
export function roleMapPath(env = process.env, platform = process.platform, home = homedir()) {
  const explicit = env.LUDIARS_MODEL_ROLES_FILE?.trim();
  if (explicit) return explicit;
  if (platform === 'win32') return join(env.LOCALAPPDATA?.trim() || join(home, 'AppData', 'Local'), 'LUDIARS', 'model-roles.json');
  return join(env.XDG_STATE_HOME?.trim() || join(home, '.local', 'state'), 'ludiars', 'model-roles.json');
}

let cache = { path: '', mtimeMs: -1, map: null };

/** Parsed `{ codex: {...}, claude: {...} }`, or null when the file is absent or invalid. */
export function readRoleMap(env = process.env) {
  const path = roleMapPath(env);
  let mtimeMs;
  try { mtimeMs = statSync(path).mtimeMs; } catch { return null; }
  if (cache.path === path && cache.mtimeMs === mtimeMs) return cache.map;
  let map = null;
  try {
    const parsed = JSON.parse(readFileSync(path, 'utf8'));
    if (parsed?.schemaVersion === 1 && parsed.roles && typeof parsed.roles === 'object') map = parsed.roles;
  } catch { map = null; }
  cache = { path, mtimeMs, map };
  return map;
}

export function roleProvider(role) {
  if (MODEL_ROLES.claude.includes(role)) return 'claude';
  if (MODEL_ROLES.codex.includes(role)) return 'codex';
  return null;
}
