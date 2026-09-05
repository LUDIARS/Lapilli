---
task: log-weaver-contract-runtime
project: Lapilli
kind: 実装
created: 2026-09-05
memory_links:
  - Augur/spec/plan/2026-09-05-live-contract-testing.md
---
# C1 log-weaver に契約ラッパー `contract()` を追加する

## 目的
Augur 設計書 (§4) の C1。`@ludiars/log-weaver` に `aspect()` と並ぶ `contract()` を足し、
事前条件 / 事後条件 / 不変条件の違反を weaver イベントとして記録する。戻り値と throw は
そのまま透過し (observe)、`enforce` のときだけ `ContractViolationError` を throw / reject する。

## 完了条件
- `contract<T, A, R>(fn, spec)` が sync / async / `this` 透過で動き、observe では fulfillment 値と
  rejection reason が `fn` と同一
- `ContractSpec` (`contractId` / pre / post / postThrow / invariant / mode / sample) と
  `Contract<A, R>` 型、`ContractViolationError` を `index.ts` から export
- 違反時 `contract violated` (ctx: contract / phase / reason / observed_at / where / rule / id / duration_ms)、
  合格時 `contract observed` (`phase: "ok"`) を emit。述語が throw したら `contract predicate threw`
  を出し合格扱いにしない (mode によらず対象関数を妨げない)
- sink の env 優先順位を明文化: `LOG_WEAVER=0` 常に無効、`LOG_WEAVER=1` は test 環境でも有効、
  それ以外は `NODE_ENV=test` / `VITEST` で no-op
- 設計書 §12 の C1-1 / C1-2 を単体テストで満たす。理由文字列に引数値・戻り値を自動で載せない

## スコープ (編集可ディレクトリ)
- packages/log-weaver/src
- packages/log-weaver/README.md (あれば)
