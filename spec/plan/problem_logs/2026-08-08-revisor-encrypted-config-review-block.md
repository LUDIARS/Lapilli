# Revisor review block: encrypted config strict reader

- 発生日: 2026-08-08
- 状態: blocked — Revisor 登録更新の認証待ち
- 影響範囲: `LUDIARS/Lapilli#334` のローカル Revisor レビュー
- 重要度: review-blocking

## 事象

`readConfigStrict` を含む暗号化設定ストアの変更に対し、Revisor が target domain の未設定を
ブロック理由として報告した。同時に、登録済みテストケース 1 件の失敗が報告され、
自動修正では進展しなかった。

最新の再審査では target domain はブロック理由から消えた。残る回帰は、Revisor に登録された
Lapilli の依存導入コマンドが pnpm workspace と不整合なことである。

## 根拠

- Revisor 通知: `1 registered test case(s) failed`
- Revisor 通知: `target domain is still missing`
- Revisor 通知: `1 changed function(s) are orphaned`
- 最新 Revisor 通知: `1 registered test case(s) failed` のみ（target domain は解消）
- Revisor 登録の読み取り結果: `install` / `test` / `typecheck` の 3 件

### 失敗ケースの切り分け（2026-08-11 追加）

3 件のうち失敗しているのは `test` のみで、`typecheck` は通過している。この非対称性が
「依存導入の不整合であってコード欠陥ではない」ことの決定的根拠になる。

- `packages/*/tsconfig.json` は `exclude` に `**/*.test.ts` を持つため、`tsc --noEmit` は
  テストファイルを型検査しない。したがって `vitest` が未導入でも `typecheck` は成功する。
- `packages/*/package.json` の `test` は `vitest run` であり、実行時に `vitest` の解決を要する。
- `pnpm-lock.yaml` の root importer (`.`) の devDependencies は `@types/node` と `typescript`
  のみで、`vitest` は各パッケージ側の importer にしか存在しない。
- Revisor 登録の `install` は `npm install --include=dev` であり、npm は `pnpm-workspace.yaml`
  を解釈せず root package.json の devDependencies しか導入しない。結果として `vitest` は
  どの `node_modules/.bin` にも配置されず、`test` だけが失敗する。

`readConfigStrict` と追加テストを再読しても失敗要因は存在しない。自動テスト修正が進展しな
かったのは、修正対象となるコード欠陥が無いためである。

## 原因

暗号化設定ストアの正本ソースと仕様は追加済みだったが、変更関数を明示的に所有する
Anatomia domain membership を登録していなかった。

Revisor の `install` は `npm install --include=dev`、`test` は `npm test`
（実体は `pnpm -r run test`）で実行された。前者は pnpm workspace の依存配置を作らないため、
後者で各パッケージの `vitest` が見つからず失敗した。Lapilli の通常手順は
`pnpm install --frozen-lockfile` であり、Revisor に登録した導入コマンドが不整合だった。

ユーザーの実行許可がないため、ローカルの単体・統合・起動テストは実施しない。

## 対応

- `packages/encrypted-config/src/store.ts` を対象とする `encrypted-config-store` domain を追加する。
- Revisor の Lapilli 登録で、`install` を `pnpm install --frozen-lockfile` に変更する。
- 現在の #334 は domain 定義追加前の `5dd688e` を評価しているため、最新コミットを再審査する。

## フォローアップ

Revisor の登録更新は Bearer workflow token を必要とする。このセッションには当該 token が
注入されておらず、利用可能な UI 操作面もないため、自動更新は行えない。認証済みの Revisor
Settings から Lapilli の登録を再保存し、`install` を `pnpm install --frozen-lockfile` に訂正する。

### 人間の判断が必要な事項（2026-08-11 時点で未決）

原因はリポジトリ外の登録内容にあるため、セッション単独では解消できない。次のいずれかを選ぶ。

1. 恒久対応（推奨）: 認証済み Revisor Settings で `install` を `pnpm install --frozen-lockfile`
   に訂正する。リポジトリ変更は不要。
2. 暫定対応: root package.json の devDependencies に `vitest` を追加し `pnpm-lock.yaml` を更新する。
   `pnpm run` は workspace root の `node_modules/.bin` も PATH に載せるため、`install` が
   `npm install --include=dev` のままでも `pnpm -r run test` が通るようになる。ただし pnpm
   monorepo の依存配置方針を変える変更であり、承認なしには行わない。

この判断が下りるまで #334 の `test` は失敗したままになる。

## 検証

- 静的検証として `git diff --check` を実施する。
- Revisor で `install`、`test`、`typecheck` が通過し、target domain が
  `encrypted-config-store` として検出されることを確認する。
