import { describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { watchEventLoopLag, type HistogramLike } from './lag.js';
import type { WeaverEvent } from './types.js';

function fakeHistogram(p99Ns: number): HistogramLike & { enabled: boolean; resets: number } {
  return {
    enabled: false,
    resets: 0,
    mean: p99Ns / 2,
    max: p99Ns,
    enable() { this.enabled = true; },
    disable() { this.enabled = false; },
    reset() { this.resets += 1; },
    percentile(p: number) { return p === 99 ? p99Ns : p99Ns / 2; },
  };
}

describe('watchEventLoopLag', () => {
  it('p99 が閾値超えの tick だけ warn を emit し、毎 tick reset する', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));

    const slow = fakeHistogram(500e6); // p99 = 500ms
    let tick: (() => void) | undefined;
    const stop = watchEventLoopLag({
      thresholdMs: 200,
      histogramFactory: () => slow,
      scheduleFn: (fn) => { tick = fn; return setInterval(() => {}, 1 << 30); },
    });

    expect(slow.enabled).toBe(true);
    tick!();
    tick!();
    expect(events).toHaveLength(2);
    expect(events[0]!.level).toBe('warn');
    expect(events[0]!.ctx).toMatchObject({ p99_ms: 500, threshold_ms: 200 });
    expect(slow.resets).toBe(2);

    stop();
    unbind();
    expect(slow.enabled).toBe(false);
  });

  it('閾値未満は無音', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));

    const fast = fakeHistogram(5e6); // p99 = 5ms
    let tick: (() => void) | undefined;
    const stop = watchEventLoopLag({
      thresholdMs: 200,
      histogramFactory: () => fast,
      scheduleFn: (fn) => { tick = fn; return setInterval(() => {}, 1 << 30); },
    });

    tick!();
    stop();
    unbind();
    expect(events).toHaveLength(0);
    expect(fast.resets).toBe(1);
  });
});
