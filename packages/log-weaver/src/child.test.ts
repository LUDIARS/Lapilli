import { describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { watchChild, type ChildLike } from './child.js';
import type { WeaverEvent } from './types.js';

class FakeChild implements ChildLike {
  pid = 4242;
  spawnargs = ['mytool', '--flag'];
  listeners = new Map<string, (...args: never[]) => void>();
  on(event: string, listener: (...args: never[]) => void): this {
    this.listeners.set(event, listener);
    return this;
  }
  fire(event: string, ...args: unknown[]): void {
    (this.listeners.get(event) as ((...a: unknown[]) => void) | undefined)?.(...args);
  }
}

describe('watchChild', () => {
  it('error イベントを emit する (spawn 失敗の致命クラッシュ→記録へ)', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));
    const child = new FakeChild();

    expect(watchChild(child, { where: 'src/run.ts:5' })).toBe(child);
    child.fire('error', new Error('spawn ENOENT'));
    unbind();

    expect(events[0]!.level).toBe('error');
    expect(events[0]!.ctx).toMatchObject({
      where: 'src/run.ts:5',
      pid: 4242,
      command: 'mytool',
      error: 'spawn ENOENT',
    });
  });

  it('非 0 exit を warn、0 exit は無音', () => {
    const events: WeaverEvent[] = [];
    const unbind = bindSink((e) => events.push(e));
    const child = new FakeChild();
    watchChild(child);

    child.fire('exit', 0, null);
    expect(events).toHaveLength(0);

    child.fire('exit', 1, null);
    child.fire('exit', null, 'SIGKILL');
    unbind();

    expect(events).toHaveLength(2);
    expect(events[0]!.ctx).toMatchObject({ code: 1 });
    expect(events[1]!.ctx).toMatchObject({ code: null, signal: 'SIGKILL' });
  });

  it('on が throw する変な child でも watchChild は throw しない', () => {
    const broken = { on() { throw new Error('no listeners allowed'); } };
    expect(() => watchChild(broken)).not.toThrow();
  });
});
