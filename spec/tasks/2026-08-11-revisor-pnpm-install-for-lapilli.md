---
task: revisor-pnpm-install-for-lapilli
project: Lapilli
kind: 雑用
created: 2026-08-11
memory_links: []
---
# Revisor の Lapilli 登録を pnpm install へ修正する

## 目的

Revisor に登録した Lapilli の install コマンドを `npm install --include=dev` から
`pnpm install --frozen-lockfile` へ変更し、pnpm workspace の開発依存（vitest を含む）を
登録テストで正しく利用できるようにする。

## 完了条件

- Revisor の Lapilli リポジトリ登録が `pnpm install --frozen-lockfile` を使う。
- Lapilli の root `package.json` や `pnpm-lock.yaml` に暫定依存を追加しない。
- Revisor local PR #334 の登録テストが、設定変更後の審査で実行可能になる。

## スコープ (編集可ディレクトリ)

- `spec/tasks/`（この記録のみ）
- Revisor の Lapilli リポジトリ登録設定（workflow token を持つ担当者による操作）
