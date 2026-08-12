---
task: workspace-root-vitest-fallback
project: Lapilli
kind: 実装
created: 2026-08-11
memory_links:
  - spec/plan/problem_logs/2026-08-08-revisor-encrypted-config-review-block.md
  - spec/tasks/2026-08-11-revisor-install-command-pnpm.md
---
# workspace root に vitest を置いて npm install でも test を通す (判断待ちの代替案)

## 目的

`2026-08-11-revisor-install-command-pnpm` の恒久対応が取れない場合の暫定手段を用意する。
root package.json の devDependencies に `vitest` を追加すれば、`pnpm run` は package 自身に加えて
workspace root の `node_modules/.bin` も PATH に載せるため、Revisor 登録の `install` が
`npm install --include=dev` のままでも `pnpm -r run test` が vitest を解決できる。

これは pnpm monorepo の依存配置方針を変える変更であり、ユーザの承認なしには着手しない。
恒久対応を採る場合、本 task は着手せず close する。

## 完了条件

- ユーザが恒久対応ではなく本案を採ると明示的に判断している。
- root package.json の devDependencies に `vitest` (各パッケージと同じ `^2.1.0`) が入っている。
- `pnpm-lock.yaml` の root importer (`.`) に vitest が反映され、
  `pnpm install --frozen-lockfile` が失敗しない。
- Revisor 再審査で `install` / `test` / `typecheck` の 3 件が通過する。

## スコープ (編集可ディレクトリ)

- package.json (root)
- pnpm-lock.yaml
- spec/tasks/
- spec/plan/problem_logs/
