# data: encrypted-config — config ファイルスキーマ

`@ludiars/encrypted-config` がディスクに永続化する config ファイルと、その中の
暗号化 blob の構造を定義する。DB は持たず、JSON ファイル 1 個が永続データ。

正本ソース: `packages/encrypted-config/src/store.ts` / `src/crypto.ts`

---

## 1. 保存先

- パス: env `<configPathEnv>` の値。未設定なら `process.cwd()/<defaultConfigFile>`。
  - `configPathEnv` / `defaultConfigFile` は consumer が `StoreOptions` で指定する
    (例: `TIROCINIUM_CONFIG_PATH` / `tirocinium.config.json`)。
- フォーマット: UTF-8 の JSON。書き込みは 2-space インデント + 末尾改行。
- consumer 側リポでは gitignore 対象 (シークレットを含むため)。

## 2. ファイル構造 (`ConfigFile`)

```jsonc
{
  "plain":   { "<key>": "<value>" },        // 非シークレット (平文文字列)
  "secrets": { "<key>": <EncryptedBlob> }   // シークレット (暗号化済み)
}
```

| フィールド | 型 | 説明 |
|---|---|---|
| `plain` | `Record<string, string>` | 非シークレット設定 (port / host / backend 等)。平文。 |
| `secrets` | `Record<string, EncryptedBlob>` | シークレット設定 (API キー / Bot トークン等)。AES-256-GCM 暗号化。 |

- あるキーが `plain` と `secrets` の両方に同時に存在することはない。
  `setConfig` はキーを分類変更した際、もう一方の側から `delete` する。
- 破損 / パース失敗時は `{ plain: {}, secrets: {} }` (空 config) として扱う
  (throw しない)。

## 3. 暗号化 blob 構造 (`EncryptedBlob`)

`secrets` の各値は以下の構造を持つ。アルゴリズムは AES-256-GCM、鍵は
scrypt 派生 (32 byte)。

```jsonc
{
  "v":    1,          // スキーマバージョン (現状 1 固定)
  "salt": "<base64>", // 16 byte ランダム salt (scrypt 入力)
  "iv":   "<base64>", // 12 byte ランダム IV (GCM nonce)
  "tag":  "<base64>", // GCM 認証タグ
  "data": "<base64>"  // 暗号文 (JSON.stringify した値を暗号化)
}
```

| フィールド | 型 | 由来 |
|---|---|---|
| `v` | `1` | リテラル。現状のスキーマバージョン。 |
| `salt` | base64 string | `randomBytes(16)`。呼び出しごとにユニーク。 |
| `iv` | base64 string | `randomBytes(12)`。呼び出しごとにユニーク。 |
| `tag` | base64 string | `cipher.getAuthTag()`。改竄検知に使用。 |
| `data` | base64 string | `JSON.stringify(value)` の暗号文。 |

- 同じ平文・同じ鍵でも `salt` / `iv` がランダムなため blob は毎回変わる。
- `tag` / 鍵 / `data` のいずれかが不整合だと復号は throw する (改竄 / 鍵不一致検知)。

## 4. 鍵の導出 (master secret)

- master secret = env `<masterKeyEnv>` の値。未設定なら
  `<masterSecretPrefix>:<hostname>:<username>` (マシン束縛フォールバック)。
- 実際の AES 鍵 = `scryptSync(masterSecret, salt, 32)` (blob ごとの salt を使用)。
- master secret が変わると既存 `secrets` は復号できなくなる。`readConfig` は
  復号失敗キーを silently skip する (例外を伝播させない)。

## 5. バージョニング / マイグレーション

- `EncryptedBlob.v` は現状 `1` のみ。将来スキーマ変更時はこの値を上げて分岐する。
- `isEncryptedBlob()` は `v === 1` かつ `salt` / `data` が string であることを判定基準にする。
  v 不一致の値は blob とみなされず復号対象から外れる。

## 関連

- 公開 API: [interface/encrypted-config.md](../interface/encrypted-config.md)
- 機能概要: [feature/encrypted-config-store.md](../feature/encrypted-config-store.md)
