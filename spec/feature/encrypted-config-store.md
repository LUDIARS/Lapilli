# feature: encrypted-config — ローカル暗号化 config ストア

`@ludiars/encrypted-config` パッケージの中核機能。サービスの設定値を、非シークレットは
平文・シークレットは AES-256-GCM で暗号化して、リポジトリ直下の 1 つの JSON ファイルに
保存・読み出しする。

正本ソース: `packages/encrypted-config/src/store.ts`

---

## 目的 / ユーザーストーリー

- LUDIARS の各サービス (Canalis / Tirocinium 等) が、API キーや Bot トークンといった
  シークレットを **専用の secret manager 無しで** ローカルに安全に保持したい。
- 非シークレット (port / host / backend) は平文で見えてよく、シークレットだけ暗号化したい。
- 鍵を別途配らずとも、env で master key を渡す or マシン束縛のフォールバックで運用したい。

## 振る舞い (入力 → 処理 → 出力)

consumer は `StoreOptions` (どのキーがシークletか、env 変数名、ファイル名等) を用意して
各関数に渡す。

- **書き込み** `setConfig(key, value, opts, env?)`
  - `key` が `opts.secretKeys` に含まれる → `encryptJson` して `secrets[key]` に保存、
    `plain[key]` を削除。
  - 含まれない → `plain[key]` に平文保存、`secrets[key]` を削除。
- **読み出し (全件)** `readConfig(opts, env?)`
  - ファイル未存在 → `null`。
  - 存在 → `plain` をそのまま、`secrets` を復号して 1 つの平文 map に統合して返す。
  - 復号失敗キー (master 鍵変更 / 改竄) は skip され結果に含まれない。
- **削除** `deleteConfig(key, opts, env?)` — `plain` / `secrets` 双方から該当キーを除去。
- **低レベル** `readConfigFile` / `writeConfigFile` は復号せず生の `ConfigFile` 構造を扱う。

## 制約 / 既知の制限

- 値は文字列前提 (`ResolvedConfig = Record<string, string>`)。複雑な構造は consumer が
  serialize する。
- master secret を失う / 変えると既存シークレットは復元不能 (silently skip)。
- ファイルロックは無い。並行プロセスからの同時書き込みは consumer 側で避ける。
- マシン束縛フォールバック (`prefix:hostname:user`) はマシン移行で鍵が変わる点に注意。

## 関連

- データスキーマ: [data/encrypted-config-store.md](../data/encrypted-config-store.md)
- 暗号プリミティブ: [feature/encrypted-config-crypto.md](./encrypted-config-crypto.md)
- 公開 API: [interface/encrypted-config.md](../interface/encrypted-config.md)
