# feature: mail-inbox — Gmail 読み取り

`@ludiars/mail-inbox` は Gmail API v1 を `fetchImpl` 経由で直接呼び、読み取ったメールを
共有可能な `MailSource` に変換する。分類・保存・既読化・ラベル操作・再試行判断は行わない。

## [id: SPEC-MAIL-INBOX-001] newerThan の丸めを吸収する

`search` は `newerThanEpochSec` を `after:<epoch>` として Gmail 検索クエリに追加し、取得済みの
各メールも `internalDate` が指定 epoch 秒より新しいかで再フィルタする。検索取得は最大 5 並列とする。

## [id: SPEC-MAIL-INBOX-002] 添付本体は要求時だけ取る

`loadAttachments: false`（既定値）では添付のメタデータだけを返し、attachment endpoint を呼ばない。
`true` の場合だけ上限 bytes 以下の添付を取得して `data: Buffer` を設定する。

## [id: SPEC-MAIL-INBOX-003] 認証とレート制限を区別する

401 は `MailInboxError(kind: 'auth')`、429 と rateLimitExceeded の 403 は
`MailInboxError(kind: 'rate_limit')` として失敗させる。再試行は利用側が決める。

## [id: SPEC-MAIL-INBOX-004] HTML 専用メールにも本文を返す

plain text がない場合、`text/html` を簡易的にタグ除去・entity decode して `text` として返す。
元の HTML は `html` に保持する。

## 制約

- OAuth scope は `https://www.googleapis.com/auth/gmail.readonly` のみ。
- Google API SDK は導入しない。
- 認証情報は呼び出し側から受け取り、このパッケージは環境変数を読まない。
