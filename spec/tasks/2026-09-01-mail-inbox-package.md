---
task: mail-inbox-package
project: Lapilli
kind: 機能追加
created: 2026-09-01
memory_links: [1782]
---
# Gmail メール受信パッケージを追加する

## 目的

Gmail API v1 を最小権限で読み取り、本文と添付を共通の `MailSource` として利用できる
`@ludiars/mail-inbox` を追加する。

## 完了条件

- `GmailSource`、MIME パース、refresh token provider を外部 SDK なしで提供する。
- 取得 API はすべて注入可能な `fetchImpl` を使い、ネットワークなしのテストを対で置く。
- 公開 API・Gmail 振る舞い仕様・Anatomia domain 宣言を追加する。

## 調査・設計判断

- Anatomia の再利用探索では既存の Gmail / OAuth 実装は見つからず、`encrypted-config` の package 構成だけを踏襲した。
- 新規 surface は既存 domain に属さないため、`.anatomia/domains/mail-inbox.json` にパッケージ全体の membership を登録した。

## Sol 自己検証

- `googleapis` / `google-auth-library` は package manifest に 0 件。
- 書き込み権限 scope (`gmail.modify` / `gmail.labels` / `gmail.send`) は source に 0 件。
- `process.env` は source に 0 件。
- `src/*.test.ts` は 4 本で、MIME multipart + 添付、HTML-only、refresh token cache、newerThan の再フィルタ、添付未取得、401/429 の分類を対象にした。
- `pnpm --filter @ludiars/mail-inbox typecheck` は node_modules 未導入のため `tsc` 未検出で実行不能（依存インストールは本委託の範囲外）。
- Anatomia verify は `pass: true`（全 gate pass）。
