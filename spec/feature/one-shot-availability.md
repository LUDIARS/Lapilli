# Model-agnostic one-shot availability

User instruction: neco, 2026-10-21. Supporting domain: one-shot.
Value OS-AUTO-01: text processing continues when one subscription is exhausted,
without silently changing a provider-specific tool or permission contract.

`runAvailableOneShot` is an opt-in entry for provider-neutral calls. Existing
spawn/exec APIs and explicit model IDs keep their current contracts. The caller
supplies quota observations and provider executors; Lapilli neither owns account
credentials nor depends on Concordia HTTP endpoints. Executors normalize their
CLI's output and preserve the caller's tool/permission restrictions.

- OS-A1: absent quota windows are omitted only with provider evidence. Unknown,
  expired, future-dated or stale observations cannot establish availability.
  At least one applicable window is required. Model-scoped windows also apply.
- OS-A2: ultra Opus/Astra, high Sonnet/Sol, medium and low Haiku/Luna. Select the
  greatest minimum remaining percentage within the requested tier, stable ties.
  No cross-tier fallback. Explicit provider/model pins are never substituted.
- OS-A3: select before execution; invoke exactly one executor. No retry after an
  error, timeout, partial output or unknown result. Return the effective selection.
- OS-A4: quota observation, cancellation and executor deadlines belong to the
  caller. Cancelled calls never start another provider. No provider executor may
  be silently replaced with a different tool-capability implementation.

Rollback: restore the prior package revision and consumer pin. Tests use injected
observations/executors, without inference, credentials, network or live processes.
