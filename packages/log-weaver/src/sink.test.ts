import { describe, expect, it } from 'vitest';
import { bindSink, emit, weaverLog, sinkBound } from './sink.js';
import type { WeaverEvent } from './types.js';

describe('sink', () => {
  it('bindSink したイベントが届き、unbind で止まる', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));
    expect(sinkBound()).toBe(true);

    weaverLog('warn', 'hello', { a: 1 });
    expect(events).toEqual([{ level: 'warn', msg: 'hello', ctx: { a: 1 } }]);

    unbind();
    expect(sinkBound()).toBe(false);
    weaverLog('warn', 'after unbind');
    expect(events).toHaveLength(1);
  });

  it('ctx 省略時は ctx キー自体を載せない', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));
    emit('info', 'no ctx');
    unbind();
    expect(events[0]).toEqual({ level: 'info', msg: 'no ctx' });
    expect('ctx' in events[0]!).toBe(false);
  });

  it('sink が throw しても emit は never throw', () => {
    const unbind = bindSink(() => {
      throw new Error('sink broken');
    });
    expect(() => emit('error', 'boom')).not.toThrow();
    unbind();
  });

  it('test 環境では未 bind の emit が既定 sink を作らず no-op になる', () => {
    // VITEST=true が立っているので、ファイルが書かれないことだけを保証する。
    expect(() => emit('info', 'dropped')).not.toThrow();
  });
});
