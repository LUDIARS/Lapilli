# interface: @ludiars/mail-inbox — 公開 API

Gmail API v1 の読み取りと MIME の抽出を行う `@ludiars/mail-inbox` の公開境界。
正本ソースは `packages/mail-inbox/src/index.ts`。

```ts
import {
  GmailSource, createRefreshTokenProvider, parseGmailPayload, decodeBase64Url,
  type AccessTokenProvider, type MailAttachment, type MailMessage, type MailSource,
  type SearchOptions,
} from '@ludiars/mail-inbox';
```

- `MailSource.search(query, opts)` は検索結果を `MailMessage[]` で返す。
- `MailSource.get(id, opts)` は存在しない ID に `null` を返す。
- `MailSource.loadAttachment(messageId, attachmentId)` は添付本文を `Buffer` で返す。
- `GmailSource` は `fetchImpl` と `AccessTokenProvider` を注入して構築する。既定は `globalThis.fetch`、user ID は `me`。
- `createRefreshTokenProvider` は refresh token を access token に交換し、期限 60 秒前までメモリキャッシュする。
- `parseGmailPayload` は Gmail の MIME payload から plain text、HTML、添付メタデータを取り出す。
- `MailMessage.html` は元メールの未加工 HTML であり、安全化済みではない。利用側は表示前にサニタイズする。
- `MailInboxError.kind` は `auth` / `rate_limit` / `not_found` / `network` / `invalid_response` のいずれかである。

`SearchOptions` の `maxResults` は既定 50・上限 500、`loadAttachments` は既定 false、
`maxAttachmentBytes` は既定 15 MiB。`newerThanEpochSec` は Gmail クエリと `internalDate` の両方で絞り込む。
