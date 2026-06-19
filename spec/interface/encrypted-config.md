# interface: @ludiars/encrypted-config — 公開 API

パッケージ `@ludiars/encrypted-config` が `.` エントリ (`dist/index.js`) から export する
公開境界。HTTP 等は持たず、npm ライブラリの export 群が contract。

正本ソース: `packages/encrypted-config/src/index.ts` (再 export 元 `crypto.ts` / `store.ts`)

import 例:
```ts
import { setConfig, readConfig, encryptJson, type StoreOptions } from '@ludiars/encrypted-config';
```

---

## 1. 暗号プリミティブ (`crypto.ts` 由来)

```ts
function encryptJson(value: unknown, masterSecret: string): EncryptedBlob;
function decryptJson<T = unknown>(blob: EncryptedBlob, masterSecret: string): T;
function isEncryptedBlob(x: unknown): x is EncryptedBlob;

interface EncryptedBlob {
  v: 1;
  salt: string;  // base64
  iv: string;    // base64
  tag: string;   // base64
  data: string;  // base64
}
```

- `decryptJson` は鍵不一致 / 改竄で **throw** する。
- `isEncryptedBlob` は `v === 1` かつ `salt`/`data` が string か判定する型ガード。

## 2. config ストア (`store.ts` 由来)

```ts
function resolveConfigPath(opts: StoreOptions, env?: NodeJS.ProcessEnv): string;
function resolveMasterSecret(opts: StoreOptions, env?: NodeJS.ProcessEnv): string;
function readConfigFile(opts: StoreOptions, env?: NodeJS.ProcessEnv): ConfigFile;
function writeConfigFile(cfg: ConfigFile, opts: StoreOptions, env?: NodeJS.ProcessEnv): void;
function readConfig(opts: StoreOptions, env?: NodeJS.ProcessEnv): ResolvedConfig | null;
function setConfig(key: string, value: string, opts: StoreOptions, env?: NodeJS.ProcessEnv): void;
function deleteConfig(key: string, opts: StoreOptions, env?: NodeJS.ProcessEnv): void;
```

型:

```ts
interface ConfigFile {
  plain: Record<string, string>;
  secrets: Record<string, EncryptedBlob>;
}

type ResolvedConfig = Record<string, string>;

interface StoreOptions {
  secretKeys: Set<string>;       // 暗号化保存するキー。それ以外は plain。
  configPathEnv: string;         // config パスの env 変数名
  masterKeyEnv: string;          // master secret の env 変数名
  defaultConfigFile: string;     // 既定 config ファイル名
  masterSecretPrefix: string;    // フォールバック prefix → "prefix:hostname:user"
}
```

### 契約 (返り値 / 副作用)

| 関数 | 返り / 副作用 |
|---|---|
| `resolveConfigPath` | env override か `cwd/defaultConfigFile` の絶対 / 相対パス文字列。 |
| `resolveMasterSecret` | env override か `prefix:hostname:user`。 |
| `readConfigFile` | 生の `ConfigFile`。未存在 / 破損で `{ plain:{}, secrets:{} }`。 |
| `writeConfigFile` | ファイルへ 2-space JSON + 末尾改行で書き込み (親 dir を mkdir)。 |
| `readConfig` | 復号済み平文 map。**ファイル未存在で `null`**。復号失敗キーは skip。 |
| `setConfig` | 1 キーを書き込み (secretKeys なら暗号化)。逆側分類から delete。 |
| `deleteConfig` | 1 キーを plain/secrets 双方から除去。 |

- `env` 引数は省略時 `process.env`。テストや明示注入のため第 N 引数で渡せる。

## 関連

- データスキーマ: [data/encrypted-config-store.md](../data/encrypted-config-store.md)
- 機能: [feature/encrypted-config-store.md](../feature/encrypted-config-store.md) /
  [feature/encrypted-config-crypto.md](../feature/encrypted-config-crypto.md)
