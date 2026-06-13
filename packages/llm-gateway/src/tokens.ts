/**
 * トークン数の概算。
 *
 * 正確なトークナイザは持たない (モデル依存・重い)。 予算判定 (要約を起こすか、
 * routing で strong に上げるか) には桁が合えば十分なので、 文字種別の経験則で見積もる。
 *
 * - CJK 文字は概ね 1.5 文字 / token (日本語は密)
 * - それ以外 (ラテン・記号・空白) は概ね 4 文字 / token
 */

/** CJK (漢字・かな・全角) 系コードポイントか。 */
function isCjk(code: number): boolean {
  return (
    (code >= 0x3040 && code <= 0x30ff) || // ひらがな + カタカナ
    (code >= 0x3400 && code <= 0x4dbf) || // CJK 拡張 A
    (code >= 0x4e00 && code <= 0x9fff) || // CJK 統合漢字
    (code >= 0xf900 && code <= 0xfaff) || // CJK 互換漢字
    (code >= 0xff00 && code <= 0xffef) // 全角英数・記号
  );
}

/** 文字列のトークン数を概算する。 */
export function estimateTokens(text: string): number {
  if (!text) return 0;
  let cjk = 0;
  let other = 0;
  for (const ch of text) {
    const code = ch.codePointAt(0);
    if (code !== undefined && isCjk(code)) cjk++;
    else other++;
  }
  return Math.ceil(cjk / 1.5 + other / 4);
}
