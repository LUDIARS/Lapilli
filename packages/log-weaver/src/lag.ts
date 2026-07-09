// event-loop lag probe (安定化チェックリスト §6)。
//
// 同期 DB API や重い同期処理がイベントループを塞ぐと、HTTP 全応答が止まり
// 「外から見ると死んでいる」状態になる。perf_hooks.monitorEventLoopDelay を
// 定期スナップショットし、p99 が閾値を超えた時だけ warn を emit する
// (常時 emit はノイズ)。timer は unref 済みでプロセス終了を妨げない。

import { monitorEventLoopDelay } from 'node:perf_hooks';
import { emit } from './sink.js';

/** monitorEventLoopDelay 互換の最小面。テストでフェイクを渡す。 */
export interface HistogramLike {
  enable(): void;
  disable(): void;
  reset(): void;
  percentile(p: number): number;
  mean: number;
  max: number;
}

export interface LagWatchOptions {
  /** p99 (ms) がこの値以上で warn。既定 200。 */
  thresholdMs?: number;
  /** スナップショット間隔 (ms)。既定 30000。 */
  intervalMs?: number;
  /** テスト用の差し替え。 */
  histogramFactory?: (opts: { resolution: number }) => HistogramLike;
  /** テスト用の差し替え。 */
  scheduleFn?: (fn: () => void, ms: number) => NodeJS.Timeout;
}

const nsToMs = (ns: number): number => Math.round((ns / 1e6) * 100) / 100;

/** 監視を開始し、stop 関数を返す。 */
export function watchEventLoopLag(options: LagWatchOptions = {}): () => void {
  const thresholdMs = options.thresholdMs ?? 200;
  const intervalMs = options.intervalMs ?? 30_000;
  const factory = options.histogramFactory ?? monitorEventLoopDelay;
  const schedule = options.scheduleFn ?? setInterval;

  const histogram = factory({ resolution: 10 });
  histogram.enable();

  const timer = schedule(() => {
    try {
      const p99 = nsToMs(histogram.percentile(99));
      if (p99 >= thresholdMs) {
        emit('warn', 'event-loop lag exceeded threshold', {
          p99_ms: p99,
          p50_ms: nsToMs(histogram.percentile(50)),
          mean_ms: nsToMs(histogram.mean),
          max_ms: nsToMs(histogram.max),
          threshold_ms: thresholdMs,
        });
      }
      histogram.reset();
    } catch {
      /* never throw from logging */
    }
  }, intervalMs);
  timer.unref?.();

  return () => {
    clearInterval(timer);
    histogram.disable();
  };
}
