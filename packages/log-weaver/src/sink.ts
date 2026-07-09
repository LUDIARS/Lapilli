// sink 管理と emit。
//
// - bindSink() で任意の sink (Vg writer 等) を束ねる。
// - 未 bind のまま emit されたら既定 sink (logsDir への JSONL 追記) に落ちる。
//   注入されたコードからのイベントを「bind し忘れたので消えた」にしないため。
// - test 環境 (NODE_ENV=test / VITEST) と LOG_WEAVER=0 では既定 sink を無効化
//   する。明示的に bindSink されたものは常に生きる (テストが自前 sink を挿す)。
// - emit は never throw。ログが本体を壊すことは絶対にない。

import { appendFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import type { WeaverEvent, WeaverLevel, WeaverSink } from './types.js';

let boundSink: WeaverSink | null = null;
let defaultSink: WeaverSink | null | undefined;

function defaultDisabled(): boolean {
  return (
    process.env.LOG_WEAVER === '0' ||
    process.env.NODE_ENV === 'test' ||
    process.env.VITEST === 'true'
  );
}

/** logsDir は Vestigium と同じ規約 (env VESTIGIUM_LOGS_DIR > cwd/logs)。 */
export function resolveLogsDir(): string {
  return process.env.VESTIGIUM_LOGS_DIR ?? join(process.cwd(), 'logs');
}

/**
 * 既定 sink: `${logsDir}/weaver.jsonl` へ 1 イベント 1 行追記する。
 * Vg 非依存のフォールバックだが、同じ logsDir に書くので Concordia の
 * observability (file tail) からはそのまま見える。
 * イベントは稀 (warn/error 中心) なので同期追記で十分、順序も保たれる。
 */
export function fileJsonlSink(path?: string): WeaverSink {
  let resolved: string | null = null;
  return (event) => {
    if (resolved === null) {
      const dir = resolveLogsDir();
      mkdirSync(dir, { recursive: true });
      resolved = path ?? join(dir, 'weaver.jsonl');
    }
    const line = JSON.stringify({
      ts: new Date().toISOString(),
      source: 'log-weaver',
      service: process.env.LOG_WEAVER_SERVICE ?? undefined,
      ...event,
    });
    appendFileSync(resolved, `${line}\n`);
  };
}

/** sink を束ねる。以後の emit はここへ流れる。解除関数を返す。 */
export function bindSink(sink: WeaverSink): () => void {
  boundSink = sink;
  return () => {
    if (boundSink === sink) boundSink = null;
  };
}

export function sinkBound(): boolean {
  return boundSink !== null;
}

/**
 * 1 イベント emit。sink が throw しても呼び出し元には絶対に漏らさない。
 */
export function emit(level: WeaverLevel, msg: string, ctx?: Record<string, unknown>): void {
  try {
    const event: WeaverEvent = ctx === undefined ? { level, msg } : { level, msg, ctx };
    if (boundSink) {
      boundSink(event);
      return;
    }
    if (defaultSink === undefined) {
      defaultSink = defaultDisabled() ? null : fileJsonlSink();
    }
    defaultSink?.(event);
  } catch {
    /* never throw from logging */
  }
}

/**
 * 注入コード向けの表玄関。Augur の source injection が挿す呼び出しはこれ。
 */
export function weaverLog(level: WeaverLevel, msg: string, ctx?: Record<string, unknown>): void {
  emit(level, msg, ctx);
}
