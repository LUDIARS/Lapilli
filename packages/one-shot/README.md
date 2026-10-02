# @ludiars/one-shot

Subscription-backed Claude Code / Codex launch boundary. Consumers retain their prompt,
response parsing, streaming, timeout, cancellation, cost reporting and process ownership.
Use `spawnOneShot` in place of `node:child_process.spawn` for `claude -p` / `codex exec`.
Arguments and stdio remain explicit; shell execution is disabled. There is no retry or API fallback.

Pass a role (`opus`, `sonnet`, `haiku`, `fable`, `astra`, `sol`, `luna`) for a centrally
maintained default. Explicit model IDs remain exact overrides. The role snapshot is from
Concordia's active catalog on 2026-10-03; `LUDIARS_ONESHOT_MODEL_<ROLE>` provides an explicit
deployment override. Changing the catalog does not automatically update this package.
Callers must use `resolveModel` when reporting the effective model, rather than the role.

The child uses its installed CLI's saved account login. API-key and cloud-provider selectors
are removed from the child environment. CLI configuration can also affect authentication;
provision the service user's CLI for subscription login. Credentials are not read or copied
by this library. Missing credentials fail through the CLI; no paid API fallback is supplied.

The process owner must subscribe to error/close, drain output, feed stdin, kill on deadline
or cancellation, and release its timers/listeners. Interactive CLI sessions are outside scope.

## Alternatives

Claude Agent SDK can replace a hand-written process protocol inside this boundary; Codex's
TypeScript SDK likewise offers typed invocation. Neither removes startup or subscription
limits by itself. A persistent worker is useful only after measuring startup cost, and must
start a fresh conversation per job to avoid sharing context. Do not extract OAuth tokens to
call provider APIs directly. Keep the provider-supported CLI/SDK authentication boundary.

- https://support.claude.com/en/articles/15036540-use-the-claude-agent-sdk-with-your-claude-plan
- https://developers.openai.com/codex/noninteractive

Acceptance: consumers use the shared boundary; old consumer default pins become roles;
stdin/output/permissions/cancellation contracts remain unchanged. Automated execution and
live subscription calls require the task's applicable test authorization.
