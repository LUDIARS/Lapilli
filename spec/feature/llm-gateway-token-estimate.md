# feature: llm-gateway — トークン数概算

`@ludiars/llm-gateway` の `estimateTokens`。文字種別の経験則で文字列のトークン数を
概算する。予算判定 (要約を起こすか / strong に上げるか) の補助。

正本ソース: `packages/llm-gateway/src/tokens.ts` / 設計背景: `packages/llm-gateway/DESIGN.md` §3 計測

---

## 目的 / ユーザーストーリー

- 正確なトークナイザはモデル依存で重い。予算判定には「桁が合えば十分」。
- consumer は、外部依存ゼロ・同期で軽い概算でトークン予算を見積もりたい。
- `rollingSummary` の既定トークン推定としても内部利用される。

## 振る舞い (入力 → 処理 → 出力)

入力: `text: string`。

処理 (文字種別の経験則):
- 空文字 → `0`。
- CJK 系コードポイント (ひらがな/カタカナ 0x3040–0x30FF、CJK 拡張A 0x3400–0x4DBF、
  統合漢字 0x4E00–0x9FFF、互換漢字 0xF900–0xFAFF、全角 0xFF00–0xFFEF) は密として **1.5 文字/token**。
- それ以外 (ラテン・記号・空白) は **4 文字/token**。
- `ceil(cjk / 1.5 + other / 4)` を返す。

出力: `number` (概算トークン数、非負整数)。

## 制約 / 既知の制限

- あくまで概算。課金額の精算には使えない (モデル固有トークナイザではない)。
- code point ベースで surrogate pair も 1 文字単位で走査する (`for...of`)。

## 関連

- 利用元: [feature/llm-gateway-rolling-summary.md](./llm-gateway-rolling-summary.md) /
  [feature/llm-gateway-tier-routing.md](./llm-gateway-tier-routing.md)
- 公開 API: [interface/llm-gateway.md](../interface/llm-gateway.md)
