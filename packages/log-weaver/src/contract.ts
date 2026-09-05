// 契約プログラミングのラッパー — オンライブ契約テスト。
//
// Augur 設計書 (spec/plan/2026-09-05-live-contract-testing.md §4) の C1。
// aspect() と同じく観測のみで意味論を変えない (mode: "observe", 既定)。
// mode: "enforce" のときだけ違反で ContractViolationError を throw / reject する
// (テスト専用、本番では使わない)。
//
// contractId (manifest の契約 id) は ctx.contract、Where.id (marker id) は
// ctx.id に分けて出す。述語は理由文字列以外の値 (引数・戻り値) をログに
// 自動で載せない — reason は述語が明示的に返した文字列だけを通す。

import { emit } from './sink.js';
import type { Where } from './types.js';

export interface ContractSpec<A extends unknown[], R> {
  contractId: string;
  pre?: (...args: A) => true | false | string;
  post?: (result: Awaited<R>, ...args: A) => true | false | string;
  postThrow?: (err: unknown, ...args: A) => true | false | string;
  invariant?: (self: unknown, ...args: A) => true | false | string;
  mode?: 'observe' | 'enforce';
  sample?: number;
}

export type Contract<A extends unknown[], R> = Omit<ContractSpec<A, R>, 'contractId'>;

// @implements SPEC-LOG-WEAVER-001
export class ContractViolationError extends Error {
  readonly contractId: string;
  readonly phase: 'pre' | 'post' | 'postThrow' | 'invariant';
  readonly reason: string;

  // @implements SPEC-LOG-WEAVER-001
  constructor(contractId: string, phase: ContractViolationError['phase'], reason: string) {
    super(`contract ${contractId} violated (${phase}): ${reason}`);
    this.name = 'ContractViolationError';
    this.contractId = contractId;
    this.phase = phase;
    this.reason = reason;
  }
}

type Verdict = true | false | string;

// @implements SPEC-LOG-WEAVER-002
function baseCtx(spec: ContractSpec<unknown[], unknown> & Where, startedMs: number): Record<string, unknown> {
  return {
    contract: spec.contractId,
    where: spec.where,
    rule: spec.rule,
    id: spec.id,
    observed_at: new Date().toISOString(),
    duration_ms: Math.round(performance.now() - startedMs),
  };
}

// @implements SPEC-LOG-WEAVER-002
function emitObserved(spec: ContractSpec<unknown[], unknown> & Where, startedMs: number): void {
  emit('debug', 'contract observed', { ...baseCtx(spec, startedMs), phase: 'ok' });
}

// @implements SPEC-LOG-WEAVER-002
function emitViolated(
  spec: ContractSpec<unknown[], unknown> & Where,
  startedMs: number,
  phase: ContractViolationError['phase'],
  reason: string,
): void {
  emit('error', 'contract violated', { ...baseCtx(spec, startedMs), phase, reason });
}

// @implements SPEC-LOG-WEAVER-002
function emitPredicateThrew(
  spec: ContractSpec<unknown[], unknown> & Where,
  startedMs: number,
  phase: ContractViolationError['phase'],
): void {
  emit('warn', 'contract predicate threw', {
    ...baseCtx(spec, startedMs),
    phase: 'predicate',
    predicate_phase: phase,
  });
}

/**
 * 述語を安全に評価し、throw は値を保持せず結果フラグへ変換する。
 * @implements SPEC-LOG-WEAVER-001
 */
function evalPredicate(
  predicate: ((...a: never[]) => Verdict) | undefined,
  args: unknown[],
): { verdict: Verdict | undefined; threw: boolean } {
  if (!predicate) return { verdict: undefined, threw: false };
  try {
    return { verdict: (predicate as (...a: unknown[]) => Verdict)(...args), threw: false };
  } catch {
    return { verdict: undefined, threw: true };
  }
}

// @implements SPEC-LOG-WEAVER-001
function reasonOf(verdict: Verdict): string | null {
  if (verdict === true) return null;
  if (verdict === false) return 'predicate returned false';
  return verdict;
}

// @implements SPEC-LOG-WEAVER-003
function shouldSample(sample: number | undefined): boolean {
  if (sample === undefined || sample >= 1) return true;
  if (sample <= 0) return false;
  return Math.random() < sample;
}

/**
 * fn を契約ラッパーに包む。sync/async どちらも可、`this` を透過する。
 * observe (既定) では fn の戻り値・throw・fulfillment 値・rejection reason は
 * そのまま伝播する。enforce では違反時に ContractViolationError を throw / reject する。
 * @implements SPEC-LOG-WEAVER-001
 * @implements SPEC-LOG-WEAVER-002
 * @implements SPEC-LOG-WEAVER-003
 */
export function contract<T, A extends unknown[], R>(
  fn: (this: T, ...args: A) => R,
  spec: ContractSpec<A, R> & Where,
): (this: T, ...args: A) => R {
  const mode = spec.mode ?? 'observe';
  const enforcing = mode === 'enforce';
  const untypedSpec = spec as unknown as ContractSpec<unknown[], unknown> & Where;

  // @implements SPEC-LOG-WEAVER-001
  return function (this: T, ...args: A): R {
    const started = performance.now();
    if (!shouldSample(spec.sample)) {
      return fn.apply(this, args);
    }

    let callViolated = false;
    let predicateThrew = false;

    const preCheck = evalPredicate(spec.pre as ((...a: never[]) => Verdict) | undefined, args);
    if (preCheck.threw) {
      predicateThrew = true;
      emitPredicateThrew(untypedSpec, started, 'pre');
    } else if (preCheck.verdict !== undefined) {
      const reason = reasonOf(preCheck.verdict);
      if (reason !== null) {
        callViolated = true;
        emitViolated(untypedSpec, started, 'pre', reason);
        if (enforcing) throw new ContractViolationError(spec.contractId, 'pre', reason);
      }
    }

    const invPre = evalPredicate(spec.invariant as ((...a: never[]) => Verdict) | undefined, [this, ...args]);
    if (invPre.threw) {
      predicateThrew = true;
      emitPredicateThrew(untypedSpec, started, 'invariant');
    } else if (invPre.verdict !== undefined) {
      const reason = reasonOf(invPre.verdict);
      if (reason !== null) {
        callViolated = true;
        emitViolated(untypedSpec, started, 'invariant', reason);
        if (enforcing) throw new ContractViolationError(spec.contractId, 'invariant', reason);
      }
    }

    // @implements SPEC-LOG-WEAVER-001
    // @implements SPEC-LOG-WEAVER-002
    const checkPostSuccess = (result: Awaited<R>): void => {
      let postCallViolated = false;
      let violationReason = '';
      let violationPhase: ContractViolationError['phase'] = 'post';

      const postCheck = evalPredicate(spec.post as ((...a: never[]) => Verdict) | undefined, [result, ...args]);
      if (postCheck.threw) {
        predicateThrew = true;
        emitPredicateThrew(untypedSpec, started, 'post');
      } else if (postCheck.verdict !== undefined) {
        const reason = reasonOf(postCheck.verdict);
        if (reason !== null) {
          callViolated = true;
          postCallViolated = true;
          violationReason = reason;
          emitViolated(untypedSpec, started, 'post', reason);
        }
      }

      const invPost = evalPredicate(spec.invariant as ((...a: never[]) => Verdict) | undefined, [this, ...args]);
      if (invPost.threw) {
        predicateThrew = true;
        emitPredicateThrew(untypedSpec, started, 'invariant');
      } else if (invPost.verdict !== undefined) {
        const reason = reasonOf(invPost.verdict);
        if (reason !== null) {
          callViolated = true;
          postCallViolated = true;
          violationReason = reason;
          violationPhase = 'invariant';
          emitViolated(untypedSpec, started, 'invariant', reason);
        }
      }

      if (!callViolated && !predicateThrew) {
        emitObserved(untypedSpec, started);
      } else if (postCallViolated && enforcing) {
        throw new ContractViolationError(spec.contractId, violationPhase, violationReason);
      }
    };

    // @implements SPEC-LOG-WEAVER-001
    // @implements SPEC-LOG-WEAVER-002
    const checkPostThrow = (err: unknown): void => {
      const check = evalPredicate(spec.postThrow as ((...a: never[]) => Verdict) | undefined, [err, ...args]);
      if (check.threw) {
        predicateThrew = true;
        emitPredicateThrew(untypedSpec, started, 'postThrow');
        return;
      }
      if (check.verdict === undefined) return;
      const reason = reasonOf(check.verdict);
      if (reason !== null) {
        callViolated = true;
        emitViolated(untypedSpec, started, 'postThrow', reason);
        if (enforcing) throw new ContractViolationError(spec.contractId, 'postThrow', reason);
      } else if (!callViolated && !predicateThrew) {
        emitObserved(untypedSpec, started);
      }
    };

    let result: R;
    try {
      result = fn.apply(this, args);
    } catch (err) {
      checkPostThrow(err);
      throw err;
    }

    if (result instanceof Promise) {
      return result.then(
        // @implements SPEC-LOG-WEAVER-001
        (value) => {
          checkPostSuccess(value as Awaited<R>);
          return value;
        },
        // @implements SPEC-LOG-WEAVER-001
        (err: unknown) => {
          checkPostThrow(err);
          throw err;
        },
      ) as R;
    }

    checkPostSuccess(result as Awaited<R>);
    return result;
  };
}
