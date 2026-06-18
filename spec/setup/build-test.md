# setup: ビルド / テスト / 型チェック

Lapilli は pnpm workspaces の monorepo。ルートから全パッケージを一括操作する。

正本ソース: ルート `package.json` / `pnpm-workspace.yaml` / `tsconfig.base.json` /
各パッケージ `package.json`

---

## 前提

- Node.js 22 (publish CI が `node-version: '22'`)。
- pnpm 9 (publish CI が `pnpm/action-setup@v4` version 9)。
- workspace は `packages/*` (`pnpm-workspace.yaml`)。

## インストール

```sh
pnpm install
```
CI では `pnpm install --frozen-lockfile` (lockfile 固定)。

## ルートスクリプト (全パッケージ再帰)

ルート `package.json`:

| コマンド | 実体 | 内容 |
|---|---|---|
| `pnpm build` | `pnpm -r run build` | 各パッケージを `tsc -p tsconfig.json` で `dist/` へ。 |
| `pnpm test` | `pnpm -r run test` | 各パッケージを `vitest run`。 |
| `pnpm typecheck` | `pnpm -r run typecheck` | 各パッケージを `tsc --noEmit`。 |

## パッケージ単体スクリプト

各 `packages/<pkg>/package.json` は同名スクリプトを持つ:
- `build` … `tsc -p tsconfig.json` (`dist/index.js` + `.d.ts` を出力)
- `typecheck` … `tsc --noEmit`
- `test` … `vitest run`

## TypeScript 設定

`tsconfig.base.json` (各パッケージが extends):
- `target: ES2022` / `module: NodeNext` / `moduleResolution: NodeNext`
- `strict: true`、`declaration: true` (型定義出力)、`declarationMap` / `sourceMap` 有効。
- ESM パッケージ (`"type": "module"`)。相対 import は `.js` 拡張子付き。

## 出力 / gitignore

- ビルド成果物は各パッケージの `dist/` (gitignore 済)。`package.json` の `files: ["dist"]`
  で publish 時はこれだけ同梱。
- `node_modules/` `dist/` `*.js.map` `*.d.ts.map` `logs/` は gitignore。
