# interface: @ludiars/llm-gateway — 公開 API

パッケージ `@ludiars/llm-gateway` が `.` エントリ (`dist/index.js`) から export する
公開境界。transport 非依存 — HTTP / SDK は呼ばず、純ロジックの export 群が contract。

正本ソース: `packages/llm-gateway/src/index.ts` (再 export 元 `types.ts` / `tokens.ts` /
`ordering.ts` / `summary.ts` / `routing.ts`)

import 例:
```ts
import { orderSegments, pickTier, rollingSummary, estimateTokens, type Segment } from '@ludiars/llm-gateway';
```

---

## 1. 共通型 (`types.ts` 由来)

```ts
type ChatRole = 'system' | 'user' | 'assistant';

interface ChatMessage { role: ChatRole; content: string; }

type Stability = 'fixed' | 'semi' | 'volatile';

interface Segment {
  stability: Stability;
  role: ChatRole;
  text: string;
}
```

## 2. トークン概算 (`tokens.ts` 由来)

```ts
function estimateTokens(text: string): number;
```
CJK 重み (1.5 文字/token) と非 CJK (4 文字/token) の経験則。空文字は 0。

## 3. prefix-cache 順序付け (`ordering.ts` 由来)

```ts
function orderSegments(segments: Segment[]): OrderedPrompt;

interface OrderedPrompt {
  messages: ChatMessage[];
  cacheBreakpoint: number;  // messages[0..cacheBreakpoint-1] が安定プレフィックス
}
```
安定度順 (fixed→semi→volatile) に整列・隣接結合し、安定プレフィックス長を算出。

## 4. ローリング要約 (`summary.ts` 由来)

```ts
function rollingSummary(
  turns: HistoryTurn[],
  summarize: Summarizer,
  opts: RollingSummaryOptions,
): Promise<RollingSummaryResult>;

interface HistoryTurn { role: ChatRole; content: string; }

interface RollingSummaryOptions {
  maxTokens: number;
  keepRecent: number;
  estimateTokens?: (text: string) => number;  // 既定は内蔵 estimateTokens
}

interface RollingSummaryResult {
  summary: string | null;
  recent: HistoryTurn[];
  compacted: boolean;
}

type Summarizer = (turns: HistoryTurn[]) => Promise<string>;
```
予算超過時のみ古いターンを `summarize` (consumer 注入) で 1 文字列に畳む。

## 5. tier ルーティング (`routing.ts` 由来)

```ts
function pickTier(input: RoutingInput, rules?: RoutingRules): Tier;

type Tier = 'cheap' | 'strong';

interface RoutingInput {
  kind: string;
  inputTokens?: number;
  forceTier?: Tier;
}

interface RoutingRules {
  strongKinds?: string[];        // 既定: ['converge','facilitate','aufhebung','synthesize']
  strongAboveTokens?: number;    // 既定: 8000
}
```
優先順: `forceTier` → `strongKinds` 一致 → `inputTokens > strongAboveTokens` → `cheap`。

## スコープ外 (contract に含まれないもの)

- 実 HTTP / SDK 呼び出し、`cache_control` 付与、モデル ID 解決、RAG 索引、
  プロンプト圧縮 (LLMLingua 連携) — consumer / serving 側の責務 (DESIGN.md §5)。

## 関連

- 機能: [feature/llm-gateway-prefix-ordering.md](../feature/llm-gateway-prefix-ordering.md) /
  [llm-gateway-rolling-summary](../feature/llm-gateway-rolling-summary.md) /
  [llm-gateway-tier-routing](../feature/llm-gateway-tier-routing.md) /
  [llm-gateway-token-estimate](../feature/llm-gateway-token-estimate.md)
- 設計背景: `packages/llm-gateway/DESIGN.md`
