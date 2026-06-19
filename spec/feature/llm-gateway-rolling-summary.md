# feature: llm-gateway — 会話履歴のローリング要約

`@ludiars/llm-gateway` の `rollingSummary`。会話履歴がトークン予算を超えたとき、
直近 N ターンを生で残し、それより古い部分を要約 1 個に畳む。

正本ソース: `packages/llm-gateway/src/summary.ts` / 設計背景: `packages/llm-gateway/DESIGN.md` §3②

---

## 目的 / ユーザーストーリー

- エージェント型ループでは毎ターン履歴が丸ごと再送され入力トークンを食う。
- consumer は、古いやり取りを要約に畳んで入力サイズを抑えたい。要約に使う LLM 呼び出しは
  自前のもの (任意の transport / モデル) を注入したい。

## 振る舞い (入力 → 処理 → 出力)

入力:
- `turns: HistoryTurn[]` — 会話履歴 (`role` + `content`)。
- `summarize: Summarizer` — 古いターン群を 1 文字列に畳む非同期関数 (consumer 注入)。
- `opts: RollingSummaryOptions` — `maxTokens` (履歴予算)・`keepRecent` (生で残す直近数)・
  任意の `estimateTokens` (既定は内蔵ヒューリスティック)。

処理:
1. 履歴の総トークン推定が `maxTokens` 以下 → 何もせず素通し。
2. 超過 → 直近 `keepRecent` ターンを `recent` に残し、残りを `older` とする。
3. `older` が空 (履歴が `keepRecent` 以下) → 畳む対象が無いので要約しない。
4. それ以外 → `summarize(older)` を await して要約を得る。

出力: `RollingSummaryResult`
- `summary: string | null` — 畳んだ要約 (畳まなければ `null`)。
- `recent: HistoryTurn[]` — 生で残した直近ターン。
- `compacted: boolean` — 実際に要約が起きたか。

## 制約 / 既知の制限

- 要約の品質・プロンプトは consumer が注入する `summarize` 次第 (本 lib は呼ばない)。
- トークン推定は概算 (`estimateTokens`)。厳密なトークナイザは使わない。
- `keepRecent` は負値でも 0 にクランプされる。

## 関連

- トークン概算: [feature/llm-gateway-token-estimate.md](./llm-gateway-token-estimate.md)
- 公開 API: [interface/llm-gateway.md](../interface/llm-gateway.md)
