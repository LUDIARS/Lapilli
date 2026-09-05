# feature: log-weaver — 安定稼働 probe 群

`@ludiars/log-weaver` の runtime probe。長時間稼働サービスを止める「細かい停止バグ」
(未処理 rejection・spawn エラー・async タイマー/リスナーの静かな死・イベントループ閉塞)
を、call site を書き換えずに **外部注入で観測** するための部品集。

正本ソース: `packages/log-weaver/src/` / 設計の親元: Augur `spec/feature/log-injection.md` /
検出対象の根拠: Concordia `spec/plan/problems/stability-checklist.md`

---

## 目的 / ユーザーストーリー

- 運用しながらログを集め、AI (Concordia error pipeline / Augur planning) に
  停止バグの自動修正シグナルを渡したい。
- Concordia で実証した「AOP 的に計測を外部注入する」パターン (HTTP middleware /
  console capture / hook) を、どのプロジェクトにも 1 行〜数行で入れられる形に一般化する。
- ログ経路は Vestigium (Vg) JSONL。probe はイベントを作るだけで transport を持たない。

## probe 一覧 (入力 → 処理 → 出力)

| probe | 何を観測するか | 挙動への影響 |
|---|---|---|
| `installProcessSafetyNet()` | unhandledRejection / uncaughtException / process warning | rejection は「記録して生かす」(`crashOnRejection: true` で記録後 rethrow)。uncaught は `uncaughtExceptionMonitor` による観測専用 — 既定のクラッシュは変えない |
| `watchEventLoopLag()` | event-loop lag p99 (perf_hooks histogram, 30s 周期) | なし (閾値超過時のみ warn を emit、timer は unref 済) |
| `guardAsync(fn, where?)` | async listener / タイマー body の throw・rejection | 例外を記録して**飲む** — listener・interval を生かし続けるのが目的 |
| `guardInterval(fn, ms, where?)` | 同上 (setInterval 省略形) | 同上 |
| `watchChild(child, where?)` | spawn の error イベント・非 0 / シグナル exit | error リスナーを付ける = 「リスナー無しで親ごと死ぬ」を「記録されたイベント」に変える |
| `aspect(fn, { name, slowMs? })` | 任意関数の所要時間・throw/rejection | なし — 戻り値も例外も透過。slow 超過とエラー時のみ emit |
| `contract(fn, spec)` | 事前条件 / 事後条件 / throw 後条件 / 不変条件 | `observe` は戻り値と例外を透過。`enforce` は違反時だけ `ContractViolationError` を throw/reject |

観測用 probe と emit 経路は **never throw** (ログが本体を壊さない)。明示的な contract の
`enforce` mode だけは、契約違反を呼び出し側へ通知するために例外を投げる。

## `./auto` — 1 行注入のランタイム層

```ts
import '@ludiars/log-weaver/auto';
```

- import 時 (同期) に `installProcessSafetyNet()` + `watchEventLoopLag()`。
- sink 解決: `@ludiars/vestigium` が import できれば `install()` して writer へ bind、
  無ければ既定 file sink へフォールバック (下記)。bind 解決前のイベントも file sink に落ちる。
- 無効化: `LOG_WEAVER=0` / `NODE_ENV=test` / `VITEST=true`。
- serviceCode: env `LOG_WEAVER_SERVICE` (無ければ `unknown`)。

## sink 解決規則

1. `bindSink(sink)` 済み → そこへ。
2. 未 bind → 既定 sink `${VESTIGIUM_LOGS_DIR || cwd/logs}/weaver.jsonl` へ 1 行 JSONL 追記
   (`{ ts, source: "log-weaver", service, level, msg, ctx }`)。Vg と同じ logsDir 規約なので、
   Vg 依存が無いプロジェクトでも Concordia observability の file tail から見える。
3. test 環境 / `LOG_WEAVER=0` では既定 sink は no-op (明示 bind は常に有効 — テストが使う)。
   `LOG_WEAVER=1` は test 環境の自動無効化より優先する。

## [id: SPEC-LOG-WEAVER-001] 契約述語と呼び出し結果を透過する

`contract()` は pre / post / postThrow / invariant を同期関数と async 関数で評価し、`this` と
observe mode の戻り値・fulfillment 値・throw/rejection reason を透過する。enforce mode は
違反した phase と理由を持つ `ContractViolationError` に置き換える。述語自身の throw は対象関数を妨げない。

## [id: SPEC-LOG-WEAVER-002] 契約観測イベントから値を漏らさない

違反は `contract violated`、全評価の合格は `contract observed`、述語自身の throw は
`contract predicate threw` として記録する。ctx は contract / phase / reason / observed_at /
where / rule / id / duration_ms などのメタデータに限り、引数・戻り値・throw 値・述語例外の
メッセージや stack を自動で載せない。

## [id: SPEC-LOG-WEAVER-003] 契約評価を呼び出し単位で sample する

`sample` は呼び出し単位の評価率とし、0 以下では全述語とイベントを省略、1 以上または省略時は
常に評価し、その間は指定率で評価する。sample 対象外でも対象関数自体は通常どおり呼び出す。

## 機微情報ルール

Vg CLAUDE.md のルールを継承: ctx に token / PII / コマンド・プロンプトの生データを入れない。
probe が自動で載せるのは rule / where / pid などのメタデータのみ。対象例外を観測する既存 probe は
エラーメッセージ・stack を載せるが、契約述語は引数や戻り値を参照できるため例外詳細を載せない。

## テスト

`packages/log-weaver/src/*.test.ts`。実プロセスを落とさずに検証するため、
safety net はリスナー登録数と直接呼び出し、lag はフェイク histogram/timer、
child はフェイク ChildProcess を注入する。
