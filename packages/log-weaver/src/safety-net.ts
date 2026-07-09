// プロセスレベルの安全網 (安定化チェックリスト §1)。
//
// - unhandledRejection: Node 22 既定では 1 発でプロセス終了。リスナーを登録して
//   「記録して生かす」に変える (crashOnRejection: true で記録後に rethrow)。
// - uncaughtException: `uncaughtExceptionMonitor` を使う。これは観測専用で、
//   既定のクラッシュ挙動を変えない (状態が壊れた後に走り続けるのは危険なので、
//   生かすかどうかはホスト側の判断に残す)。
//
// 二重 install は no-op。uninstall 関数を返す。

import { emit } from './sink.js';

export interface SafetyNetOptions {
  /** true なら unhandledRejection を記録した上で throw し直す (既定 false = 生かす)。 */
  crashOnRejection?: boolean;
}

let installed: (() => void) | null = null;

export function installProcessSafetyNet(options: SafetyNetOptions = {}): () => void {
  if (installed) return installed;

  const onRejection = (reason: unknown): void => {
    const e = reason instanceof Error ? reason : new Error(String(reason));
    emit('error', 'unhandled promise rejection', { error: e.message, stack: e.stack });
    if (options.crashOnRejection) throw reason;
  };
  const onUncaughtMonitor = (err: Error, origin: string): void => {
    emit('error', 'uncaught exception', { error: err.message, stack: err.stack, origin });
  };
  const onWarning = (warning: Error): void => {
    emit('warn', 'process warning', { error: warning.message, name: warning.name });
  };

  process.on('unhandledRejection', onRejection);
  process.on('uncaughtExceptionMonitor', onUncaughtMonitor);
  process.on('warning', onWarning);

  installed = () => {
    process.removeListener('unhandledRejection', onRejection);
    process.removeListener('uncaughtExceptionMonitor', onUncaughtMonitor);
    process.removeListener('warning', onWarning);
    installed = null;
  };
  return installed;
}
