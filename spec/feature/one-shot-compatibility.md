# One-shot consumer compatibility

The one-shot supporting domain owns the shared CLI launch boundary. A consumer
must be able to migrate without losing an explicit context-window suffix or a
CLI-specific Codex model identifier. Role defaults resolve centrally; explicit
IDs remain unchanged and the provider CLI decides their availability. Known
cross-provider models and malformed argument values are rejected.

Both ESM import and CommonJS require expose the same synchronous public entry
on Node >=22.12. The package contains no top-level asynchronous initialization.
Model compatibility and CommonJS loading are checked in models.test.js without
starting a CLI, using credentials or invoking inference. Revisor runs these
with the existing package test suite. Rollback restores the package revision;
consumer submodules pin the chosen revision independently.
