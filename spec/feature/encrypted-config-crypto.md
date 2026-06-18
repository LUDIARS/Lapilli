# feature: encrypted-config — JSON 暗号化プリミティブ

`@ludiars/encrypted-config` が公開する低レベルの暗号化 / 復号ユーティリティ。
store 機能の土台だが、単体でも任意の JSON 値の暗号化に使える。

正本ソース: `packages/encrypted-config/src/crypto.ts`

---

## 目的 / ユーザーストーリー

- 任意の JSON シリアライズ可能な値を、master secret 1 つで暗号化 / 復号したい。
- 改竄や鍵不一致を確実に検出したい (黙って壊れた値を返さない)。
- 受け取った未知の値が「暗号化 blob か否か」を型ガードで判定したい。

## 振る舞い (入力 → 処理 → 出力)

- **`encryptJson(value, masterSecret)`**
  - `value` を `JSON.stringify` → ランダム salt(16B)/iv(12B) を生成
  - `scrypt(masterSecret, salt, 32)` で AES-256 鍵を導出
  - AES-256-GCM で暗号化、認証タグを取得
  - → `EncryptedBlob` (`v/salt/iv/tag/data`、各 base64) を返す
- **`decryptJson<T>(blob, masterSecret)`**
  - blob の salt から鍵を再導出 → タグを検証しつつ復号 → `JSON.parse`
  - → 元の値 (型引数 `T`) を返す
  - 鍵不一致 / 改竄 (タグ不一致) → **throw**
- **`isEncryptedBlob(x)`** — `x` が `v===1` かつ `salt`/`data` が string か判定する型ガード。

## 制約 / 既知の制限

- アルゴリズムは AES-256-GCM 固定、scrypt パラメータ (N 等) は Node 既定。
- blob は同じ入力でも salt/iv がランダムなため毎回異なる (決定的暗号化ではない)。
- 失敗は throw で表現する。呼び出し側 (store の `readConfig`) は try/catch で skip する。

## 関連

- blob 構造: [data/encrypted-config-store.md](../data/encrypted-config-store.md)
- 公開 API: [interface/encrypted-config.md](../interface/encrypted-config.md)
