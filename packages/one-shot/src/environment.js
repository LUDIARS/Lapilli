// Keep OS/proxy settings while preventing accidental API billing and parent session binding.
export function subscriptionEnvironment(source = process.env) {
  const env = { ...source };
  const blocked = /^(ANTHROPIC_API_KEY|ANTHROPIC_AUTH_TOKEN|ANTHROPIC_BASE_URL|OPENAI_API_KEY|OPENAI_BASE_URL|CODEX_API_KEY|CLAUDE_CODE_USE_BEDROCK|CLAUDE_CODE_USE_VERTEX|CLAUDE_CODE_USE_FOUNDRY|CLAUDE_CODE_USE_ANTHROPIC_AWS|CLAUDECODE|CONCORDIA_HOOK|LICTOR_.*)$/i;
  for (const key of Object.keys(env)) if (blocked.test(key)) delete env[key];
  return env;
}
