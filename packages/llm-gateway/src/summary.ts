/**
 * ローリング要約。
 *
 * 会話履歴は放置すると無制限に伸び、 毎ターン全部が再送されて入力トークンを食う。
 * 直近 N ターンは生で残し、 それより古い部分を要約 1 個に畳む。
 * 要約に使う LLM 呼び出しは外から注入させる (このライブラリは transport 非依存)。
 */

import type { ChatRole } from './types.js';
import { estimateTokens } from './tokens.js';

export interface HistoryTurn {
  role: ChatRole;
  content: string;
}

export interface RollingSummaryOptions {
  /** 履歴セクションのトークン予算。 これを超えたら要約を起こす。 */
  maxTokens: number;
  /** 常に生で残す直近ターン数。 */
  keepRecent: number;
  /** トークン推定関数 (既定は内蔵ヒューリスティック)。 */
  estimateTokens?: (text: string) => number;
}

export interface RollingSummaryResult {
  /** 古いターンの要約。 畳む必要が無ければ null。 */
  summary: string | null;
  /** 生で残した直近ターン。 */
  recent: HistoryTurn[];
  /** 実際に要約が起きたか。 */
  compacted: boolean;
}

/** 古いターン群を 1 つの要約テキストに畳む関数。 */
export type Summarizer = (turns: HistoryTurn[]) => Promise<string>;

function totalTokens(turns: HistoryTurn[], est: (t: string) => number): number {
  return turns.reduce((sum, t) => sum + est(t.content), 0);
}

/**
 * 履歴がトークン予算内なら素通し、 超過していれば直近 `keepRecent` を残して
 * 残りを `summarize` で 1 個に畳む。
 */
export async function rollingSummary(
  turns: HistoryTurn[],
  summarize: Summarizer,
  opts: RollingSummaryOptions,
): Promise<RollingSummaryResult> {
  const est = opts.estimateTokens ?? estimateTokens;

  if (totalTokens(turns, est) <= opts.maxTokens) {
    return { summary: null, recent: turns, compacted: false };
  }

  const keep = Math.max(0, opts.keepRecent);
  const recent = keep > 0 ? turns.slice(-keep) : [];
  const older = keep > 0 ? turns.slice(0, -keep) : turns.slice();

  // 畳む対象が無い (履歴が keepRecent 以下) なら要約しようがない。
  if (older.length === 0) {
    return { summary: null, recent, compacted: false };
  }

  const summary = await summarize(older);
  return { summary, recent, compacted: true };
}
