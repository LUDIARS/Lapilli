/**
 * llm-gateway 共通型。
 *
 * このライブラリは transport 非依存 ── HTTP も SDK も呼ばない。
 * 「モデルへ送る前のプロンプトをどう整形するか」 だけを扱う純ロジック。
 */

/** チャットメッセージの役割。 OpenAI / Anthropic 共通の最小集合。 */
export type ChatRole = 'system' | 'user' | 'assistant';

/** モデルへ渡す 1 メッセージ。 */
export interface ChatMessage {
  role: ChatRole;
  content: string;
}

/**
 * プロンプト断片の安定度。 prefix-cache のヒット率はこの順序付けで決まる。
 *
 * - `fixed`    : 呼び出しを跨いで不変 (人格・規約・ツール定義)。 キャッシュの錨。
 * - `semi`     : セッション内では不変 (添付資料・RAG 結果)。
 * - `volatile` : 毎回変わる (会話履歴末尾・日時・今回の入力)。
 */
export type Stability = 'fixed' | 'semi' | 'volatile';

/** 安定度タグ付きのプロンプト断片。 */
export interface Segment {
  stability: Stability;
  role: ChatRole;
  text: string;
}
