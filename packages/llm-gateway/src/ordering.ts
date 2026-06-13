/**
 * prefix-cache 整形。
 *
 * KV キャッシュは「先頭からの連続プレフィックスが完全一致」したときだけ再利用できる。
 * よって不変な断片 (人格・規約) を前に、 毎回変わる断片 (履歴・日時) を後ろに並べると
 * ヒット率が最大化する。 ここではその並び替えと、 キャッシュ境界の算出だけを行う。
 */

import type { ChatMessage, Segment } from './types.js';

export interface OrderedPrompt {
  /** 安定度順 (fixed → semi → volatile) に整列・結合したメッセージ列。 */
  messages: ChatMessage[];
  /**
   * `messages[0 .. cacheBreakpoint - 1]` が「安定プレフィックス」。
   * Anthropic なら最後の安定メッセージに cache_control を打つ、
   * vLLM/Ollama なら自動 prefix-cache がこの範囲に効く。
   */
  cacheBreakpoint: number;
}

const RANK: Record<Segment['stability'], number> = {
  fixed: 0,
  semi: 1,
  volatile: 2,
};

/**
 * 断片を安定度順に並べ替え、 同役割・同安定区分の隣接断片を 1 メッセージに結合する。
 * 空文字の断片は捨てる。 元の順序は同安定度内で保持する (安定ソート)。
 */
export function orderSegments(segments: Segment[]): OrderedPrompt {
  const ordered = segments
    .filter((s) => s.text.trim().length > 0)
    .map((s, i) => ({ s, i }))
    .sort((a, b) => RANK[a.s.stability] - RANK[b.s.stability] || a.i - b.i)
    .map((x) => x.s);

  const messages: ChatMessage[] = [];
  const stableFlags: boolean[] = [];

  for (const seg of ordered) {
    const stable = seg.stability !== 'volatile';
    const last = messages[messages.length - 1];
    if (last && last.role === seg.role && stableFlags[stableFlags.length - 1] === stable) {
      last.content += `\n\n${seg.text}`;
    } else {
      messages.push({ role: seg.role, content: seg.text });
      stableFlags.push(stable);
    }
  }

  let cacheBreakpoint = 0;
  for (const flag of stableFlags) {
    if (!flag) break;
    cacheBreakpoint++;
  }

  return { messages, cacheBreakpoint };
}
