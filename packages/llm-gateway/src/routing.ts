/**
 * tier ルーティング。
 *
 * 全タスクを最上位モデルで回すと高い。 易しいタスク (短い発話生成・分類・要約) は
 * 安いモデル、 難しいタスク (収束・止揚・統合) や大入力だけ強いモデルへ振り分ける。
 * ここではどちらの tier を選ぶかの判定だけを行う (実際のモデル ID 解決は呼び出し側)。
 */

export type Tier = 'cheap' | 'strong';

export interface RoutingInput {
  /** タスク種別 (例: "utterance" | "classify" | "summarize" | "converge")。 */
  kind: string;
  /** 入力のおおよそのトークン数。 大きいほど強いモデル寄り。 */
  inputTokens?: number;
  /** 明示指定があれば最優先。 */
  forceTier?: Tier;
}

export interface RoutingRules {
  /** 無条件で strong にする種別。 */
  strongKinds?: string[];
  /** この閾値を超える入力は strong にする。 */
  strongAboveTokens?: number;
}

const DEFAULT_RULES: Required<RoutingRules> = {
  strongKinds: ['converge', 'facilitate', 'aufhebung', 'synthesize'],
  strongAboveTokens: 8000,
};

/** タスクに対して cheap / strong いずれの tier を使うかを決める。 */
export function pickTier(input: RoutingInput, rules?: RoutingRules): Tier {
  if (input.forceTier) return input.forceTier;

  const merged = { ...DEFAULT_RULES, ...rules };
  if (merged.strongKinds.includes(input.kind)) return 'strong';
  if (input.inputTokens !== undefined && input.inputTokens > merged.strongAboveTokens) {
    return 'strong';
  }
  return 'cheap';
}
