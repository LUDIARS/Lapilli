// @ludiars/log-weaver — 共通型。
//
// WeaverEvent は Vestigium (Vg) writer の write() が受ける形と互換
// ({ level, msg, ctx })。sink を Vg に bind すればそのまま JSONL へ流れる。
// ctx には機微情報 (token / PII / コマンド・プロンプト生データ) を入れない
// のが Vg 側のルール。probe が自動で載せるのは rule / file / anchor /
// エラーメッセージ等のメタデータのみ。

export type WeaverLevel = 'trace' | 'debug' | 'info' | 'warn' | 'error';

export interface WeaverEvent {
  level: WeaverLevel;
  msg: string;
  ctx?: Record<string, unknown>;
}

/** イベントの送り先。throw してはならない (emit 側でも握るが二重の防御)。 */
export type WeaverSink = (event: WeaverEvent) => void;

/**
 * 注入箇所のメタデータ。Augur の source injection が `file:line` 等を、
 * 手書きの呼び出しが任意のラベルを渡す。
 */
export interface Where {
  /** `src/foo.ts:41` のような位置ラベル。 */
  where?: string;
  /** Augur inject の rule 名 (silent-catch 等)。 */
  rule?: string;
  /** Augur inject の point id。 */
  id?: string;
  [key: string]: unknown;
}
