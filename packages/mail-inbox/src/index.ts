export { decodeBase64Url } from './base64url.js';
export { MailInboxError, type MailInboxErrorKind } from './errors.js';
export { GmailSource } from './gmail-source.js';
export { parseGmailPayload } from './mime-parse.js';
export { createRefreshTokenProvider } from './oauth-refresh.js';
export type {
  AccessTokenProvider,
  GmailHeader,
  GmailMessagePartBody,
  GmailMessagePayload,
  GmailSourceOptions,
  MailHistoryChange,
  MailHistoryOptions,
  MailHistoryPage,
  MailAttachment,
  MailMessage,
  MailSource,
  MailWatchOptions,
  MailWatchRegistration,
  OAuthRefreshCredentials,
  ParsedPayload,
  SearchOptions,
} from './types.js';
