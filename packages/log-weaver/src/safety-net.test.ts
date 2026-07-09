import { afterEach, describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { installProcessSafetyNet } from './safety-net.js';
import type { WeaverEvent } from './types.js';

let uninstall: (() => void) | null = null;

afterEach(() => {
  uninstall?.();
  uninstall = null;
});

describe('installProcessSafetyNet', () => {
  it('unhandledRejection / uncaughtExceptionMonitor / warning のリスナーを登録し、uninstall で外す', () => {
    const before = {
      rejection: process.listenerCount('unhandledRejection'),
      uncaught: process.listenerCount('uncaughtExceptionMonitor'),
      warning: process.listenerCount('warning'),
    };
    uninstall = installProcessSafetyNet();
    expect(process.listenerCount('unhandledRejection')).toBe(before.rejection + 1);
    expect(process.listenerCount('uncaughtExceptionMonitor')).toBe(before.uncaught + 1);
    expect(process.listenerCount('warning')).toBe(before.warning + 1);

    uninstall();
    uninstall = null;
    expect(process.listenerCount('unhandledRejection')).toBe(before.rejection);
    expect(process.listenerCount('uncaughtExceptionMonitor')).toBe(before.uncaught);
    expect(process.listenerCount('warning')).toBe(before.warning);
  });

  it('二重 install は同じ uninstall を返し、リスナーを重複させない', () => {
    uninstall = installProcessSafetyNet();
    const count = process.listenerCount('unhandledRejection');
    const second = installProcessSafetyNet();
    expect(second).toBe(uninstall);
    expect(process.listenerCount('unhandledRejection')).toBe(count);
  });

  it('rejection ハンドラが理由を emit する', () => {
    const events: WeaverEvent[] = [];
    const unbindSink = bindSink((e) => events.push(e));
    uninstall = installProcessSafetyNet();

    // 実 rejection を起こすとテストランナー側が拾うので、登録された
    // リスナーを直接呼んで挙動を検証する。
    const handler = process
      .listeners('unhandledRejection')
      .at(-1) as (reason: unknown, promise: Promise<unknown>) => void;
    handler(new Error('stray rejection'), Promise.resolve());
    unbindSink();

    expect(events[0]!.level).toBe('error');
    expect(events[0]!.msg).toBe('unhandled promise rejection');
    expect(events[0]!.ctx).toMatchObject({ error: 'stray rejection' });
  });
});
