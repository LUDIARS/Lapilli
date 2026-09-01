# @ludiars/mail-inbox

Gmail API v1 からメール本文と添付メタデータを読み取る、小さな共有ライブラリです。
メールの分類、保存、既読化、ラベル変更は行いません。

```ts
import { GmailSource, createRefreshTokenProvider } from '@ludiars/mail-inbox';

const auth = createRefreshTokenProvider({ clientId, clientSecret, refreshToken });
const source = new GmailSource({ auth });
const messages = await source.search('in:inbox');
```

Google Cloud で Gmail API を有効化し、OAuth のデスクトップクライアントを作成します。認可時は
`https://www.googleapis.com/auth/gmail.readonly` だけを要求し、得た refresh token は呼び出し側の
安全な設定ストアで管理してください。

メール本文、HTML、ヘッダー、添付ファイル名はすべて信頼できない入力として扱ってください。
特に `MailMessage.html` は元メールの未加工 HTML であり、画面へ表示する前に利用側でサニタイズが必要です。
