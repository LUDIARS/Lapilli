---
task: revisor-install-command-pnpm
project: Lapilli
kind: 雑用
created: 2026-08-11
memory_links:
  - spec/plan/problem_logs/2026-08-08-revisor-encrypted-config-review-block.md
---
# Revisor 登録の install コマンドを pnpm へ訂正する

## 目的

Revisor に登録された Lapilli の `install` が `npm install --include=dev` になっており、
npm は `pnpm-workspace.yaml` を解釈せず root package.json の devDependencies しか導入しない。
その結果 `vitest` がどの `node_modules/.bin` にも配置されず、登録済み `test`
(`pnpm -r run test` → 各パッケージの `vitest run`) だけが失敗する。
これが `LUDIARS/Lapilli#334` のレビューブロッカーの根本原因であり、リポジトリ側に欠陥は無い。

同じ登録の `typecheck` が通過している事実が裏付けになる。`packages/*/tsconfig.json` は
`exclude` に `**/*.test.ts` を持つため `tsc --noEmit` は vitest を必要としない。

## 完了条件

- 認証済み Revisor Settings で Lapilli の `install` が `pnpm install --frozen-lockfile` になっている。
- 再審査で `install` / `test` / `typecheck` の 3 件がすべて通過する。
- 本 task と対になる暫定対応 (`2026-08-11-workspace-root-vitest-fallback`) を採らない判断が確定し、
  当該 task を close する。

## スコープ (編集可ディレクトリ)

- リポジトリ変更なし (Revisor 側の登録内容のみ)
- spec/tasks/
- spec/plan/problem_logs/

## 備考

登録更新は Bearer workflow token を必要とする。委託セッションには当該 token が注入されず、
利用可能な UI 操作面も無いため自動化できない。人間の操作が必須。
