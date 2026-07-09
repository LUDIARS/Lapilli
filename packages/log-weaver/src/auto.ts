// 副作用モジュール — 1 行注入のためのランタイム層一式。
//
//   import '@ludiars/log-weaver/auto';
//
// これだけで (1) プロセス安全網 (2) event-loop lag 監視 が入り、sink は
// 以下の順で解決される:
//   1. @ludiars/vestigium が import できる → install() して writer へ bind
//   2. 無ければ既定の file JSONL sink (logsDir/weaver.jsonl) にフォールバック
// 安全網は import 時点で同期に入る (bind 解決は非同期でも取りこぼさない —
// 未 bind 中のイベントは file sink へ落ちる)。
//
// 無効化: LOG_WEAVER=0、または test 環境 (NODE_ENV=test / VITEST)。
// serviceCode は env LOG_WEAVER_SERVICE (無ければ 'unknown')。

import { bindSink } from './sink.js';
import { installProcessSafetyNet } from './safety-net.js';
import { watchEventLoopLag } from './lag.js';

interface VestigiumLike {
  install(options: Record<string, unknown>): {
    writer: { write(event: Record<string, unknown>): void };
  };
}

const DISABLED =
  process.env.LOG_WEAVER === '0' ||
  process.env.NODE_ENV === 'test' ||
  process.env.VITEST === 'true';

if (!DISABLED) {
  installProcessSafetyNet();
  watchEventLoopLag();

  // 変数経由の specifier にして、vestigium を devDependency に持たない
  // consumer でも型解決/バンドル解決を壊さない。
  const vestigiumSpecifier = '@ludiars/vestigium';
  void import(/* @vite-ignore */ vestigiumSpecifier)
    .then((mod: unknown) => {
      const vestigium = mod as Partial<VestigiumLike>;
      if (typeof vestigium.install !== 'function') return;
      const vg = vestigium.install({
        serviceCode: process.env.LOG_WEAVER_SERVICE ?? 'unknown',
        captureConsole: false,
      });
      bindSink((event) => vg.writer.write({ ...event }));
    })
    .catch(() => {
      /* vestigium 不在 → file sink フォールバックのまま */
    });
}
