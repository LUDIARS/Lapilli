// async callback のガード。
//
// EventEmitter listener / setInterval body に async 関数を渡すと、emit 側の
// try/catch は同期 throw しか捕まえず、rejection は unhandledRejection として
// プロセスを落とす (安定化チェックリスト §1)。guardAsync は body 全体を
// try/catch し、例外を「記録して飲む」。listener やタイマーを生かし続けるのが
// 目的の probe なので、rethrow はしない。

import { emit } from './sink.js';
import type { Where } from './types.js';

function errorCtx(err: unknown, where?: Where): Record<string, unknown> {
  const e = err instanceof Error ? err : new Error(String(err));
  return { ...where, error: e.message, stack: e.stack };
}

/**
 * callback を「throw/reject を emit して飲む」wrapper に包む。
 * 戻り値は常に resolve する Promise (呼び出し側の await を壊さない)。
 */
export function guardAsync<A extends unknown[]>(
  fn: (...args: A) => unknown,
  where?: Where,
): (...args: A) => Promise<void> {
  return async (...args: A): Promise<void> => {
    try {
      await fn(...args);
    } catch (err) {
      emit('error', 'guarded callback threw', errorCtx(err, where));
    }
  };
}

/** setInterval(guardAsync(fn), ms) の省略形。timer は返すが unref はしない。 */
export function guardInterval(
  fn: () => unknown,
  intervalMs: number,
  where?: Where,
): NodeJS.Timeout {
  return setInterval(guardAsync(fn, where), intervalMs);
}
