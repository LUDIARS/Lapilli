// 汎用 AOP wrapper — 計測の外部注入。
//
// Concordia の HTTP middleware 計測 (duration + slow 閾値 + エラー) を任意の
// 関数へ一般化したもの。関数の意味論は変えない: 戻り値/throw はそのまま
// 透過し、観測だけを差し込む。emit されるのは「遅い」か「失敗した」時のみ。

import { emit } from './sink.js';
import type { Where } from './types.js';

export interface AspectOptions extends Where {
  /** イベントに載せる名前 (必須)。 */
  name: string;
  /** duration がこの値 (ms) 以上で warn。省略時は slow 計測なし。 */
  slowMs?: number;
}

function report(opts: AspectOptions, startedMs: number, err: unknown): void {
  const durationMs = Math.round(performance.now() - startedMs);
  const { name, slowMs, ...where } = opts;
  if (err !== undefined) {
    const e = err instanceof Error ? err : new Error(String(err));
    emit('error', 'aspect target threw', {
      ...where, name, duration_ms: durationMs, error: e.message, stack: e.stack,
    });
  } else if (slowMs !== undefined && durationMs >= slowMs) {
    emit('warn', 'aspect target exceeded slow threshold', {
      ...where, name, duration_ms: durationMs, slow_ms: slowMs,
    });
  }
}

/**
 * fn を計測付き wrapper に包む。sync/async どちらも可。
 * throw / rejection は記録した上で「そのまま」呼び出し元へ伝播する
 * (guardAsync と違い、aspect は観測のみで挙動を変えない)。
 */
export function aspect<A extends unknown[], R>(
  fn: (...args: A) => R,
  options: AspectOptions,
): (...args: A) => R {
  return (...args: A): R => {
    const started = performance.now();
    try {
      const result = fn(...args);
      if (result instanceof Promise) {
        return result.then(
          (value) => { report(options, started, undefined); return value; },
          (err: unknown) => { report(options, started, err ?? new Error('rejected')); throw err; },
        ) as R;
      }
      report(options, started, undefined);
      return result;
    } catch (err) {
      report(options, started, err ?? new Error('threw'));
      throw err;
    }
  };
}
