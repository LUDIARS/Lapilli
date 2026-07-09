// @ludiars/log-weaver — 公開 API。
//
// 安定稼働の観測を「外部注入」するための runtime probe 集。
// Augur の log injection framework (Augur spec/feature/log-injection.md) が
// 注入するのは weaverLog / guardAsync / watchChild と、entrypoint への
// `import '@ludiars/log-weaver/auto'` の 4 種。それ以外は手書き利用向け。

export type { WeaverEvent, WeaverLevel, WeaverSink, Where } from './types.js';
export { bindSink, emit, weaverLog, fileJsonlSink, resolveLogsDir, sinkBound } from './sink.js';
export { guardAsync, guardInterval } from './guard.js';
export { watchChild, type ChildLike } from './child.js';
export { installProcessSafetyNet, type SafetyNetOptions } from './safety-net.js';
export { watchEventLoopLag, type LagWatchOptions, type HistogramLike } from './lag.js';
export { aspect, type AspectOptions } from './aspect.js';
