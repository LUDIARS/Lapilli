import { describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { aspect } from './aspect.js';
import type { WeaverEvent } from './types.js';

function capture(): { events: WeaverEvent[]; unbind: () => void } {
  const events: WeaverEvent[] = [];
  const unbind = bindSink((e) => events.push(e));
  return { events, unbind };
}

describe('aspect', () => {
  it('速い成功は無音で、戻り値を透過する', () => {
    const { events, unbind } = capture();
    const wrapped = aspect((a: number, b: number) => a + b, { name: 'add', slowMs: 60_000 });
    expect(wrapped(1, 2)).toBe(3);
    unbind();
    expect(events).toHaveLength(0);
  });

  it('slowMs 以上かかった呼び出しを warn する', () => {
    const { events, unbind } = capture();
    const wrapped = aspect(() => 'ok', { name: 'always-slow', slowMs: 0 });
    wrapped();
    unbind();
    expect(events).toHaveLength(1);
    expect(events[0]!.level).toBe('warn');
    expect(events[0]!.ctx).toMatchObject({ name: 'always-slow', slow_ms: 0 });
  });

  it('同期 throw を記録した上で伝播する (挙動を変えない)', () => {
    const { events, unbind } = capture();
    const wrapped = aspect(() => {
      throw new Error('sync fail');
    }, { name: 'boom' });
    expect(() => wrapped()).toThrow('sync fail');
    unbind();
    expect(events[0]!.level).toBe('error');
    expect(events[0]!.ctx).toMatchObject({ name: 'boom', error: 'sync fail' });
  });

  it('rejection を記録した上で伝播し、成功 async は値を返す', async () => {
    const { events, unbind } = capture();
    const failing = aspect(async () => {
      throw new Error('async fail');
    }, { name: 'async-boom' });
    await expect(failing()).rejects.toThrow('async fail');

    const ok = aspect(async () => 'value', { name: 'async-ok' });
    await expect(ok()).resolves.toBe('value');
    unbind();

    expect(events).toHaveLength(1);
    expect(events[0]!.ctx).toMatchObject({ name: 'async-boom', error: 'async fail' });
  });
});
