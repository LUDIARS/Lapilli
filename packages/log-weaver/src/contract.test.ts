import { describe, expect, it } from 'vitest';
import { bindSink } from './sink.js';
import { contract, ContractViolationError } from './contract.js';
import type { WeaverEvent } from './types.js';

function capture(): { events: WeaverEvent[]; unbind: () => void } {
  const events: WeaverEvent[] = [];
  const unbind = bindSink((e) => events.push(e));
  return { events, unbind };
}

describe('contract observe (C1-1)', () => {
  it('合格した同期呼び出しは戻り値を透過し、contract observed を emit する', () => {
    const { events, unbind } = capture();
    const wrapped = contract((a: number, b: number) => a + b, {
      contractId: 'C-1',
      post: (result) => result > 0 || 'must be positive',
    });
    expect(wrapped(1, 2)).toBe(3);
    unbind();
    expect(events).toHaveLength(1);
    expect(events[0]!.level).toBe('debug');
    expect(events[0]!.msg).toBe('contract observed');
    expect(events[0]!.ctx).toMatchObject({ contract: 'C-1', phase: 'ok' });
  });

  it('同期 throw をそのまま伝播する', () => {
    const { events, unbind } = capture();
    const wrapped = contract(() => {
      throw new Error('boom');
    }, { contractId: 'C-1' });
    expect(() => wrapped()).toThrow('boom');
    unbind();
    expect(events).toHaveLength(0);
  });

  it('post 違反で contract violated を emit するが、戻り値はそのまま透過する (observe)', () => {
    const { events, unbind } = capture();
    const wrapped = contract((n: number) => n, {
      contractId: 'C-1',
      post: (result) => result > 0 || 'must be positive',
    });
    expect(wrapped(-1)).toBe(-1);
    unbind();
    expect(events).toHaveLength(1);
    expect(events[0]!.level).toBe('error');
    expect(events[0]!.msg).toBe('contract violated');
    expect(events[0]!.ctx).toMatchObject({ contract: 'C-1', phase: 'post', reason: 'must be positive' });
  });

  it('pre 違反でも元関数を呼び出し、戻り値を透過する (observe)', () => {
    const { events, unbind } = capture();
    const calls: number[] = [];
    const wrapped = contract((n: number) => {
      calls.push(n);
      return n * 2;
    }, {
      contractId: 'C-1',
      pre: (n) => n > 0 || 'n must be positive',
    });
    expect(wrapped(-1)).toBe(-2);
    unbind();
    expect(calls).toEqual([-1]);
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ msg: 'contract violated', ctx: { phase: 'pre' } });
  });

  it('async: fulfillment 値をそのまま透過し、違反時 contract violated を emit する', async () => {
    const { events, unbind } = capture();
    const wrapped = contract(async (n: number) => n, {
      contractId: 'C-1',
      post: (result) => result > 0 || 'must be positive',
    });
    await expect(wrapped(-1)).resolves.toBe(-1);
    unbind();
    expect(events[0]!.ctx).toMatchObject({ phase: 'post', reason: 'must be positive' });
  });

  it('async: rejection reason をそのまま伝播する', async () => {
    const { events, unbind } = capture();
    const wrapped = contract(async () => {
      throw new Error('async fail');
    }, { contractId: 'C-1' });
    await expect(wrapped()).rejects.toThrow('async fail');
    unbind();
    expect(events).toHaveLength(0);
  });

  it('postThrow 述語が違反を返すと contract violated (phase: postThrow) を emit し、元の throw は伝播する', () => {
    const { events, unbind } = capture();
    const wrapped = contract(() => {
      throw new Error('unexpected');
    }, {
      contractId: 'C-3',
      postThrow: (err) => (err instanceof Error && err.message === 'expected') || 'wrong error thrown',
    });
    expect(() => wrapped()).toThrow('unexpected');
    unbind();
    expect(events[0]!.ctx).toMatchObject({ contract: 'C-3', phase: 'postThrow', reason: 'wrong error thrown' });
  });

  it('postThrow 述語が合格したら contract observed を emit し、元の throw は伝播する', () => {
    const { events, unbind } = capture();
    const original = new Error('expected');
    const wrapped = contract(() => {
      throw original;
    }, {
      contractId: 'C-3',
      postThrow: (err) => err === original,
    });
    let thrown: unknown;
    try {
      wrapped();
    } catch (err) {
      thrown = err;
    }
    unbind();
    expect(thrown).toBe(original);
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ msg: 'contract observed', ctx: { phase: 'ok' } });
  });

  it('invariant を前後 2 回評価し、違反した回だけ報告する', () => {
    const { events, unbind } = capture();
    const obj = { count: 0 };
    const wrapped = contract(function (this: typeof obj) {
      this.count += 1;
    }, {
      contractId: 'C-4',
      invariant: (self) => (self as typeof obj).count < 1 || 'count must stay below 1',
    });
    wrapped.call(obj);
    unbind();
    const violated = events.filter((e) => e.msg === 'contract violated');
    expect(violated).toHaveLength(1);
    expect(violated[0]!.ctx).toMatchObject({ phase: 'invariant' });
  });

  it('this を透過する', () => {
    const { unbind } = capture();
    const obj = {
      value: 42,
      getValue(this: { value: number }) {
        return this.value;
      },
    };
    obj.getValue = contract(obj.getValue, { contractId: 'C-5' });
    expect(obj.getValue()).toBe(42);
    unbind();
  });

  it('述語が throw したら contract predicate threw を emit し、合格扱いにせず元関数を妨げない', () => {
    const { events, unbind } = capture();
    const wrapped = contract((n: number) => n, {
      contractId: 'C-6',
      post: () => {
        throw new Error('predicate bug');
      },
    });
    expect(wrapped(1)).toBe(1);
    unbind();
    expect(events).toHaveLength(1);
    expect(events[0]!.level).toBe('warn');
    expect(events[0]!.msg).toBe('contract predicate threw');
    expect(events[0]!.ctx).toMatchObject({ contract: 'C-6', phase: 'predicate', predicate_phase: 'post' });
  });

  it('述語の例外メッセージに含まれる値をログへ載せない', () => {
    const { events, unbind } = capture();
    const wrapped = contract((secret: string) => secret, {
      contractId: 'C-6',
      post: (result) => {
        throw new Error(`predicate failed for ${result}`);
      },
    });
    expect(wrapped('super-secret-value')).toBe('super-secret-value');
    unbind();
    expect(JSON.stringify(events)).not.toContain('super-secret-value');
  });

  it('理由文字列以外 (引数値・戻り値) を自動でログに載せない', () => {
    const { events, unbind } = capture();
    const wrapped = contract((secret: string) => `computed-${secret}`, {
      contractId: 'C-7',
      post: () => 'reason only, no values',
    });
    wrapped('super-secret-value');
    unbind();
    const json = JSON.stringify(events);
    expect(json).not.toContain('super-secret-value');
    expect(json).not.toContain('computed-super-secret-value');
  });
});

describe('contract enforce (C1-2)', () => {
  it('述語自身の throw は enforce mode でも元関数を妨げない', () => {
    const { events, unbind } = capture();
    const wrapped = contract((n: number) => n * 2, {
      contractId: 'C-6',
      mode: 'enforce',
      pre: () => {
        throw new Error('predicate bug');
      },
    });
    expect(wrapped(2)).toBe(4);
    unbind();
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ msg: 'contract predicate threw' });
  });

  it('pre 違反で元関数を呼ばず ContractViolationError を throw する', () => {
    const { unbind } = capture();
    const calls: number[] = [];
    const wrapped = contract((n: number) => {
      calls.push(n);
      return n;
    }, {
      contractId: 'C-1',
      mode: 'enforce',
      pre: (n) => n > 0 || 'n must be positive',
    });
    expect(() => wrapped(-1)).toThrow(ContractViolationError);
    unbind();
    expect(calls).toHaveLength(0);
  });

  it('post 違反で戻り値を捨てて ContractViolationError を throw する', () => {
    const { unbind } = capture();
    const wrapped = contract((n: number) => n, {
      contractId: 'C-1',
      mode: 'enforce',
      post: (result) => result > 0 || 'must be positive',
    });
    expect(() => wrapped(-1)).toThrow(ContractViolationError);
    unbind();
  });

  it('async: post 違反で元の fulfillment 値を捨てて reject する', async () => {
    const { unbind } = capture();
    const wrapped = contract(async (n: number) => n, {
      contractId: 'C-1',
      mode: 'enforce',
      post: (result) => result > 0 || 'must be positive',
    });
    await expect(wrapped(-1)).rejects.toThrow(ContractViolationError);
    unbind();
  });

  it('postThrow 違反で元の rejection reason を ContractViolationError に置き換える', async () => {
    const { unbind } = capture();
    const wrapped = contract(async () => {
      throw new Error('unexpected');
    }, {
      contractId: 'C-3',
      mode: 'enforce',
      postThrow: (err) => (err instanceof Error && err.message === 'expected') || 'wrong error thrown',
    });
    await expect(wrapped()).rejects.toThrow(ContractViolationError);
    unbind();
  });

  it('空文字の違反理由でも ContractViolationError を throw する', () => {
    const { unbind } = capture();
    const wrapped = contract(() => 1, {
      contractId: 'C-1',
      mode: 'enforce',
      post: () => '',
    });
    expect(() => wrapped()).toThrow(ContractViolationError);
    unbind();
  });

  it('post-call invariant 違反の ContractViolationError は invariant phase を保持する', () => {
    const { unbind } = capture();
    const obj = { count: 0 };
    const wrapped = contract(function (this: typeof obj) {
      this.count += 1;
    }, {
      contractId: 'C-4',
      mode: 'enforce',
      invariant: (self) => (self as typeof obj).count < 1 || 'count must stay below 1',
    });
    let thrown: unknown;
    try {
      wrapped.call(obj);
    } catch (err) {
      thrown = err;
    }
    unbind();
    expect(thrown).toBeInstanceOf(ContractViolationError);
    expect(thrown).toMatchObject({ phase: 'invariant' });
  });

  it('合格した呼び出しは通常どおり戻り値を返す', () => {
    const { unbind } = capture();
    const wrapped = contract((n: number) => n * 2, {
      contractId: 'C-1',
      mode: 'enforce',
      pre: (n) => n > 0 || 'n must be positive',
      post: (result) => result > 0 || 'must be positive',
    });
    expect(wrapped(2)).toBe(4);
    unbind();
  });
});

describe('contract observed_at / id / sample (C1-3)', () => {
  it('contractId と Where.id を別々に ctx.contract / ctx.id に載せ、observed_at を UTC ISO で付ける', () => {
    const { events, unbind } = capture();
    const wrapped = contract((n: number) => n, {
      contractId: 'C-2',
      id: 'marker-abc123',
      rule: 'contract-wrap',
      where: 'src/foo.ts:10',
    });
    wrapped(1);
    unbind();
    expect(events[0]!.ctx).toMatchObject({
      contract: 'C-2',
      id: 'marker-abc123',
      rule: 'contract-wrap',
      where: 'src/foo.ts:10',
    });
    expect(events[0]!.ctx!.observed_at).toMatch(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/);
  });

  it('sample: 0 は述語評価そのものをスキップする (常に元関数だけ実行)', () => {
    const { events, unbind } = capture();
    let predicateCalls = 0;
    const wrapped = contract((n: number) => n, {
      contractId: 'C-8',
      sample: 0,
      post: () => {
        predicateCalls += 1;
        return true;
      },
    });
    expect(wrapped(1)).toBe(1);
    unbind();
    expect(predicateCalls).toBe(0);
    expect(events).toHaveLength(0);
  });
});
