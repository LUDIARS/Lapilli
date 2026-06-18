# test: ユニットテスト

Lapilli の各パッケージは純ロジック (transport / DB / ネットワーク非依存) のライブラリで
あり、テストは **vitest によるユニットテスト** が主体。各パッケージ `src/index.test.ts` に集約。

正本ソース: `packages/encrypted-config/src/index.test.ts` /
`packages/llm-gateway/src/index.test.ts`

実行: `pnpm test` (ルート、全パッケージ `vitest run`) / パッケージ単体 `pnpm --filter <pkg> test`。
CI: publish ワークフローは build のみだが、テストは PR / ローカルで `vitest run` を実走する。

---

## 種別と担保内容

### ビルド / 型チェック
- `tsc -p tsconfig.json` (build) と `tsc --noEmit` (typecheck) で型整合とコンパイル可能性を担保。
- declaration 出力 (`.d.ts`) が成立するか = 公開 API の型が壊れていないか。

### ユニットテスト (vitest)

**@ludiars/encrypted-config** (`src/index.test.ts`):
- `encryptJson` / `decryptJson` — 文字列 / オブジェクトの round-trip、blob のユニーク性
  (salt/iv が毎回変わる)、**誤鍵 throw**、**改竄 (data 破壊) throw**。
- `isEncryptedBlob` — 正当な blob を accept、`null`/`undefined`/数値/文字列/`{}`/`v:2` を reject。
- store: `resolveConfigPath` (env override / cwd フォールバック)、`resolveMasterSecret`
  (env override / `prefix:` フォールバック)、`readConfig` (未存在で `null`)、
  `setConfig`/`readConfig` (plain は平文・secret は暗号化保存・両方復号統合・上書き・
  plain↔secret 再分類移動)、`deleteConfig` (plain/secret 削除・他キー不影響)、
  `writeConfigFile`/`readConfigFile` (write/read 構造保存・破損ファイルで空 config)。
- 一時ディレクトリ (`mkdtempSync`) + env 注入で実ファイル I/O を検証 (副作用を tmp に隔離)。

**@ludiars/llm-gateway** (`src/index.test.ts`):
- `estimateTokens` — 空文字 0、CJK が latin より密 (`ceil(8/1.5)` 等の境界値)。
- `orderSegments` — fixed→semi→volatile 整列・`cacheBreakpoint` 算出、同 role 同安定の
  隣接結合、安定境界を跨ぐ結合の抑止、空断片 drop、同安定度内の挿入順保持。
- `rollingSummary` — 予算内素通し、超過時に直近 `keepRecent` 残し古い分を要約
  (`summarize` 呼び出し対象数の検証)、履歴が `keepRecent` 以下なら非要約。
- `pickTier` — `forceTier` 優先、`strongKinds` → strong、simple → cheap、
  大入力 escalate、`rules` 上書き。

## 何を充実とみなすか

- ライブラリ種別なので、**公開 export 1 つずつに対し正常系 + 境界 / 異常系** が
  揃っているのを充実とみなす (DB / serving / E2E は対象外)。
- 暗号系は「誤鍵・改竄で確実に throw する」否定パスを必須とする (既にカバー済)。

## やること (現状の穴)

- CI で `pnpm test` を回す step は未配線 (publish ワークフローは build のみ)。PR でテストを
  ゲートしたい場合は test ワークフロー追加が follow-up。
