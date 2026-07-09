import { describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { guardAsync } from './guard.js';
import type { WeaverEvent } from './types.js';

function capture(): { events: WeaverEvent[]; unbind: () => void } {
  const events: WeaverEvent[] = [];
  const unbind = bindSink((e) => events.push(e));
  return { events, unbind };
}

describe('guardAsync', () => {
  it('async reject を emit して飲む (wrapper は resolve する)', async () => {
    const { events, unbind } = capture();
    const guarded = guardAsync(async () => {
      throw new Error('interval body failed');
    }, { where: 'src/loop.ts:10', rule: 'interval-guard' });

    await expect(guarded()).resolves.toBeUndefined();
    unbind();

    expect(events).toHaveLength(1);
    expect(events[0]!.level).toBe('error');
    expect(events[0]!.ctx).toMatchObject({
      where: 'src/loop.ts:10',
      rule: 'interval-guard',
      error: 'interval body failed',
    });
    expect(events[0]!.ctx!.stack).toBeDefined();
  });

  it('同期 throw も捕まえる', async () => {
    const { events, unbind } = capture();
    const guarded = guardAsync(() => {
      throw new Error('sync boom');
    });
    await expect(guarded()).resolves.toBeUndefined();
    unbind();
    expect(events[0]!.ctx).toMatchObject({ error: 'sync boom' });
  });

  it('成功時は何も emit せず、引数を透過する', async () => {
    const { events, unbind } = capture();
    const seen: number[] = [];
    const guarded = guardAsync(async (n: number) => {
      seen.push(n);
    });
    await guarded(42);
    unbind();
    expect(seen).toEqual([42]);
    expect(events).toHaveLength(0);
  });

  it('Error 以外の throw 値も文字列化して記録する', async () => {
    const { events, unbind } = capture();
    // eslint-disable-next-line @typescript-eslint/only-throw-error
    const guarded = guardAsync(async () => { throw 'plain string'; });
    await guarded();
    unbind();
    expect(events[0]!.ctx).toMatchObject({ error: 'plain string' });
  });
});
