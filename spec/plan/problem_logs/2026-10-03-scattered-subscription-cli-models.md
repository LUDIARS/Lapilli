# Scattered subscription CLI model defaults

- Date: 2026-10-03 (Asia/Tokyo)
- Status: partially resolved (2026-10-15)
- Area: LUDIARS one-shot Claude Code / Codex invocation
- Severity: stale model selection and inconsistent process handling

## Summary
neco reported Anatomia invoking an older Claude model and requested migration of all
LUDIARS `claude -p` and `codex exec` launches through Cc or a Lapilli library.

## Evidence
Anatomia/src/providers/claude-cli-llm.ts and providers/index.ts default to
`claude-opus-4-8`. Web/CLI entrypoints additionally pin Sonnet 4.6 and Haiku 4.5.
Concordia's active catalog on 2026-10-03 includes Opus 5.5, Sonnet 5, GPT-6 Sol/Luna.
Tracked-source inspection found independent CLI process wrappers across many services.

## Regression Context
No introducing commit has been established. This is a reported stale-model defect,
not evidence that every explicit older-model override is unintended.

## Cause
Confirmed: model defaults and CLI lifecycle code are independently maintained by consumers.

## Fix Requirements
Centralize the launch boundary in Lapilli; migrate consumer calls and default models;
retain explicit overrides and response/permission/cancellation contracts. Use saved CLI
subscription authentication without a silent API-key fallback.

## Verification
Read-only source/catalog inspection only. No live inference or tests run.

## Resolution so far (2026-10-15)
- Lapilli PR #2313 (98ed6a2, "centralize subscription CLI launch policy") added the shared
  launch boundary `packages/one-shot`; PR #2330 (07f1629) preserved consumer model
  identifiers and CommonJS compatibility. Both are merged on Lapilli main.
- Consumer migrations reviewed and merged on 2026-10-03 (about 20 consumers, e.g. Mercator,
  Pagus) are tracked in Actio 97f71781.

## Not verified
Live inference, consumer test suites and service restarts were not run as part of this record.

## Follow-up
Remaining migration scope is tracked in Actio, not here:
- 97f71781 final residual inventory across all repositories and runtime dependency check
- a96dabc7 scope for three repositories in other organisations
- 2336c397 Aestimator / Scrutator source of truth and scope
- 7e09ced6 Sartor, 6746ccd2 Calliope (project codes not yet registered)
The model catalog work (Actio 758136a6) is a separate task.
