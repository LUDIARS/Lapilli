# interface: @ludiars/log-weaver — 公開 API

パッケージ `@ludiars/log-weaver` の公開境界。transport 非依存 — イベントを作って
sink に渡すだけで、HTTP / SDK は呼ばない。Vg へは consumer (または `./auto`) が bind する。

正本ソース: `packages/log-weaver/src/index.ts` / 振る舞いの正本: `spec/feature/log-weaver-probes.md`

import 例:

```ts
import { weaverLog, guardAsync, watchChild, aspect, bindSink } from '@ludiars/log-weaver';
import '@ludiars/log-weaver/auto'; // 1 行注入 (side-effect)
```

---

## 1. イベントと sink

```ts
type WeaverLevel = 'trace' | 'debug' | 'info' | 'warn' | 'error';
interface WeaverEvent { level: WeaverLevel; msg: string; ctx?: Record<string, unknown>; }
type WeaverSink = (event: WeaverEvent) => void;

function bindSink(sink: WeaverSink): () => void;   // 解除関数を返す
function sinkBound(): boolean;
function emit(level: WeaverLevel, msg: string, ctx?: Record<string, unknown>): void;
function weaverLog(level: WeaverLevel, msg: string, ctx?: Record<string, unknown>): void; // emit の別名 (注入コードの表玄関)
function fileJsonlSink(path?: string): WeaverSink; // 既定 sink の生成器
function resolveLogsDir(): string;                 // env VESTIGIUM_LOGS_DIR > cwd/logs
```

`WeaverEvent` は Vg writer の `write()` 互換。Vg へ流すなら:

```ts
const vg = install({ serviceCode: 'myservice' });
bindSink((e) => vg.writer.write({ ...e }));
```

## 2. probe

```ts
interface Where { where?: string; rule?: string; id?: string; [k: string]: unknown; }

function installProcessSafetyNet(options?: { crashOnRejection?: boolean }): () => void;
function watchEventLoopLag(options?: {
  thresholdMs?: number;      // 既定 200
  intervalMs?: number;       // 既定 30000
  histogramFactory?: (opts: { resolution: number }) => HistogramLike; // テスト用
  scheduleFn?: (fn: () => void, ms: number) => NodeJS.Timeout;        // テスト用
}): () => void;

function guardAsync<A extends unknown[]>(fn: (...args: A) => unknown, where?: Where): (...args: A) => Promise<void>;
function guardInterval(fn: () => unknown, intervalMs: number, where?: Where): NodeJS.Timeout;

interface ChildLike { on(event: string, listener: (...args: never[]) => void): unknown; pid?: number; spawnargs?: string[]; }
function watchChild<C extends ChildLike>(child: C, where?: Where): C; // child をそのまま返す

interface AspectOptions extends Where { name: string; slowMs?: number; }
function aspect<A extends unknown[], R>(fn: (...args: A) => R, options: AspectOptions): (...args: A) => R;
```

## 3. `./auto` サブパス

export なしの side-effect モジュール。安全網 + lag 監視 + sink 自動解決。
無効化 env と解決順は `spec/feature/log-weaver-probes.md` を参照。

## 4. Augur inject が注入するシンボル

Augur の source injection (Augur `spec/feature/log-injection.md`) が生成するコードは
`weaverLog` / `guardAsync` / `watchChild` と `./auto` の import のみに依存する。
これらのシグネチャ変更は Augur 側の apply/remove と互換を壊すため semver major。
