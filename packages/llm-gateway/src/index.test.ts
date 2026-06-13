import { describe, it, expect } from 'vitest';
import { estimateTokens } from './tokens.js';
import { orderSegments } from './ordering.js';
import { rollingSummary, type HistoryTurn } from './summary.js';
import { pickTier } from './routing.js';
import type { Segment } from './types.js';

describe('estimateTokens', () => {
  it('returns 0 for empty', () => {
    expect(estimateTokens('')).toBe(0);
  });

  it('counts CJK denser than latin', () => {
    const jp = estimateTokens('日本語のテキスト'); // 8 CJK chars
    const en = estimateTokens('english text here'); // 17 latin chars
    expect(jp).toBeGreaterThan(0);
    expect(en).toBeGreaterThan(0);
    // 8 CJK / 1.5 ≈ 6 ; 17 / 4 ≈ 5
    expect(jp).toBe(Math.ceil(8 / 1.5));
  });
});

describe('orderSegments', () => {
  it('orders fixed before semi before volatile', () => {
    const segs: Segment[] = [
      { stability: 'volatile', role: 'user', text: 'history tail' },
      { stability: 'fixed', role: 'system', text: 'persona' },
      { stability: 'semi', role: 'user', text: 'attached doc' },
    ];
    const { messages, cacheBreakpoint } = orderSegments(segs);
    expect(messages[0].content).toBe('persona');
    expect(messages[0].role).toBe('system');
    // fixed(system) is one stable message, semi(user) another stable message,
    // volatile(user) is the unstable tail.
    expect(cacheBreakpoint).toBe(2);
    expect(messages[messages.length - 1].content).toBe('history tail');
  });

  it('merges adjacent same-role same-stability segments', () => {
    const segs: Segment[] = [
      { stability: 'fixed', role: 'system', text: 'rule A' },
      { stability: 'fixed', role: 'system', text: 'rule B' },
    ];
    const { messages, cacheBreakpoint } = orderSegments(segs);
    expect(messages).toHaveLength(1);
    expect(messages[0].content).toBe('rule A\n\nrule B');
    expect(cacheBreakpoint).toBe(1);
  });

  it('does not merge across stability boundary even with same role', () => {
    const segs: Segment[] = [
      { stability: 'semi', role: 'user', text: 'doc' },
      { stability: 'volatile', role: 'user', text: 'now' },
    ];
    const { messages, cacheBreakpoint } = orderSegments(segs);
    expect(messages).toHaveLength(2);
    expect(cacheBreakpoint).toBe(1);
  });

  it('drops empty segments', () => {
    const segs: Segment[] = [
      { stability: 'fixed', role: 'system', text: '   ' },
      { stability: 'volatile', role: 'user', text: 'hi' },
    ];
    const { messages, cacheBreakpoint } = orderSegments(segs);
    expect(messages).toHaveLength(1);
    expect(cacheBreakpoint).toBe(0);
  });

  it('keeps stable insertion order within same stability', () => {
    const segs: Segment[] = [
      { stability: 'fixed', role: 'system', text: 'first' },
      { stability: 'fixed', role: 'assistant', text: 'second' },
    ];
    const { messages } = orderSegments(segs);
    expect(messages.map((m) => m.content)).toEqual(['first', 'second']);
  });
});

describe('rollingSummary', () => {
  const turns: HistoryTurn[] = Array.from({ length: 10 }, (_, i) => ({
    role: 'user' as const,
    content: `turn ${i} ` + 'x'.repeat(40),
  }));

  it('passes through when under budget', async () => {
    const res = await rollingSummary(turns, async () => 'SUM', {
      maxTokens: 1_000_000,
      keepRecent: 4,
    });
    expect(res.compacted).toBe(false);
    expect(res.summary).toBeNull();
    expect(res.recent).toHaveLength(10);
  });

  it('summarizes older turns when over budget', async () => {
    let summarizedCount = 0;
    const res = await rollingSummary(
      turns,
      async (older) => {
        summarizedCount = older.length;
        return 'SUMMARY';
      },
      { maxTokens: 1, keepRecent: 4 },
    );
    expect(res.compacted).toBe(true);
    expect(res.summary).toBe('SUMMARY');
    expect(res.recent).toHaveLength(4);
    expect(summarizedCount).toBe(6);
  });

  it('does not summarize when history is within keepRecent', async () => {
    const few = turns.slice(0, 3);
    const res = await rollingSummary(few, async () => 'SUM', {
      maxTokens: 1,
      keepRecent: 4,
    });
    expect(res.compacted).toBe(false);
    expect(res.summary).toBeNull();
    expect(res.recent).toHaveLength(3);
  });
});

describe('pickTier', () => {
  it('honors forceTier', () => {
    expect(pickTier({ kind: 'utterance', forceTier: 'strong' })).toBe('strong');
  });

  it('routes strong kinds to strong', () => {
    expect(pickTier({ kind: 'converge' })).toBe('strong');
  });

  it('routes simple kinds to cheap', () => {
    expect(pickTier({ kind: 'utterance' })).toBe('cheap');
    expect(pickTier({ kind: 'classify' })).toBe('cheap');
  });

  it('escalates large inputs to strong', () => {
    expect(pickTier({ kind: 'utterance', inputTokens: 9000 })).toBe('strong');
  });

  it('respects custom rules', () => {
    expect(pickTier({ kind: 'review' }, { strongKinds: ['review'] })).toBe('strong');
    expect(
      pickTier({ kind: 'utterance', inputTokens: 3000 }, { strongAboveTokens: 2000 }),
    ).toBe('strong');
  });
});
