// Test-only role map: fake ids prove no model id is pinned in the package.
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export const FIXTURE_ROLES = Object.freeze({
  claude: { opus: 'claude-opus-test', sonnet: 'claude-sonnet-test', haiku: 'claude-haiku-test', fable: 'claude-fable-test' },
  codex: { astra: 'gpt-test-astra', sol: 'gpt-test-sol', luna: 'gpt-test-luna' },
});

export function fixtureEnv(roles = FIXTURE_ROLES) {
  const path = join(mkdtempSync(join(tmpdir(), 'one-shot-roles-')), 'model-roles.json');
  writeFileSync(path, JSON.stringify({ schemaVersion: 1, generatedAt: '2026-11-18T00:00:00Z', roles }));
  return { LUDIARS_MODEL_ROLES_FILE: path };
}

export function missingEnv() {
  return { LUDIARS_MODEL_ROLES_FILE: join(tmpdir(), 'one-shot-roles-missing', 'none.json') };
}
