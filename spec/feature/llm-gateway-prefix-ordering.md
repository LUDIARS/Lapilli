# feature: llm-gateway — prefix-cache 順序付け

`@ludiars/llm-gateway` の `orderSegments`。プロンプト断片を安定度順に並べ替え、
KV / prefix キャッシュのヒット率を最大化する。

正本ソース: `packages/llm-gateway/src/ordering.ts` / 設計背景: `packages/llm-gateway/DESIGN.md` §3①

---

## 目的 / ユーザーストーリー

- LLM の KV キャッシュは「先頭からの連続プレフィックスが完全一致」した時だけ再利用できる。
- consumer は、不変な断片 (人格・規約・ツール定義) を前に、毎回変わる断片 (履歴末尾・日時) を
  後ろに置いたプロンプトを組みたい。冒頭に動的値が 1 個でも混ざるとキャッシュが全滅する。

## 振る舞い (入力 → 処理 → 出力)

入力: `Segment[]` — 各断片に `stability` (`fixed`/`semi`/`volatile`)・`role`・`text`。

処理:
1. 空白のみの `text` 断片を捨てる。
2. 安定度順 `fixed → semi → volatile` に **安定ソート** (同安定度内は元順序保持)。
3. 同 role かつ同「安定区分 (volatile か否か)」で隣接する断片を 1 メッセージに結合
   (`\n\n` 連結)。
4. 先頭から連続する安定メッセージ数を数えて `cacheBreakpoint` とする。

出力: `OrderedPrompt`
- `messages: ChatMessage[]` — 整形済みメッセージ列。
- `cacheBreakpoint: number` — `messages[0 .. cacheBreakpoint-1]` が安定プレフィックス。
  Anthropic ならこの最後に `cache_control` を打つ、vLLM/Ollama なら自動 prefix-cache の効く範囲。

## 制約 / 既知の制限

- `cache_control` の実付与・実 API 呼び出しは行わない (transport 非依存。consumer / serving の責務)。
- 結合は role + 安定区分が一致する**隣接**断片のみ。安定境界を跨ぐ結合はしない。

## 関連

- 共通型: [feature/llm-gateway-token-estimate.md](./llm-gateway-token-estimate.md)
- 公開 API: [interface/llm-gateway.md](../interface/llm-gateway.md)
