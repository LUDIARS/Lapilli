# feature: llm-gateway — tier ルーティング

`@ludiars/llm-gateway` の `pickTier`。タスク種別と入力サイズから、安いモデル (`cheap`) と
強いモデル (`strong`) のどちらの tier を使うかを判定する。

正本ソース: `packages/llm-gateway/src/routing.ts` / 設計背景: `packages/llm-gateway/DESIGN.md` §3③

---

## 目的 / ユーザーストーリー

- 全タスクを最上位モデルで回すとコストが規模で罰される。
- consumer は、易しいタスク (短い発話生成・分類・要約) を安いモデルへ、難しいタスク
  (収束・止揚・統合) や大入力だけを強いモデルへ振り分けたい。

## 振る舞い (入力 → 処理 → 出力)

入力:
- `input: RoutingInput` — `kind` (タスク種別文字列)・任意の `inputTokens`・任意の `forceTier`。
- `rules?: RoutingRules` — 任意。`strongKinds` / `strongAboveTokens` で既定を上書き。

処理 (優先順):
1. `forceTier` 指定があればそれを返す (最優先)。
2. `kind` が `strongKinds` に含まれる → `strong`。
3. `inputTokens` が `strongAboveTokens` を超える → `strong`。
4. いずれにも該当しない → `cheap`。

既定ルール:
- `strongKinds = ['converge', 'facilitate', 'aufhebung', 'synthesize']`
- `strongAboveTokens = 8000`
- `rules` は浅いマージで既定を上書きする。

出力: `Tier` (`'cheap'` | `'strong'`)。

## 制約 / 既知の制限

- 実際のモデル ID 解決は行わない (tier 判定のみ。consumer / serving の責務)。
- `kind` は任意文字列。マッチングは完全一致。

## 関連

- 公開 API: [interface/llm-gateway.md](../interface/llm-gateway.md)
- 実利用例 (consumer): Discutere facilitator の tier 結線 (DESIGN.md §6)
