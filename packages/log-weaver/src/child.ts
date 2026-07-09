// 子プロセス監視 probe。
//
// spawn 失敗 (ENOENT / EACCES / EMFILE) は非同期の "error" イベントで届き、
// リスナーが無いと親ごと uncaughtException で落ちる (安定化チェックリスト §2)。
// watchChild は error リスナーを付けて「致命クラッシュ」を「記録されたイベント」
// に変える。exit も観測し、非 0 終了 / シグナル死を warn で残す
// (「子の全クラッシュが成功終了に見える」事故の逆張り)。

import { emit } from './sink.js';
import type { Where } from './types.js';

/** ChildProcess の構造型。テストではフェイクを渡せる。 */
export interface ChildLike {
  on(event: string, listener: (...args: never[]) => void): unknown;
  pid?: number | undefined;
  spawnargs?: string[] | undefined;
}

/**
 * 子プロセスへ error / exit の観測リスナーを付け、同じ child を返す。
 * `const child = watchChild(spawn(...))` とチェーンできる。
 */
export function watchChild<C extends ChildLike>(child: C, where?: Where): C {
  const base = {
    ...where,
    pid: child.pid,
    command: child.spawnargs?.[0],
  };
  try {
    child.on('error', ((err: Error) => {
      emit('error', 'child process error', { ...base, error: err.message, stack: err.stack });
    }) as (...args: never[]) => void);
    child.on('exit', ((code: number | null, signal: string | null) => {
      if (code === 0) return;
      emit('warn', 'child process exited abnormally', { ...base, code, signal });
    }) as (...args: never[]) => void);
  } catch {
    /* never throw from logging */
  }
  return child;
}
