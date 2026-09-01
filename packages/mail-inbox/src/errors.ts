// @implements SPEC-MAIL-INBOX-003
export type MailInboxErrorKind = 'auth' | 'rate_limit' | 'not_found' | 'network' | 'invalid_response';

export class MailInboxError extends Error {
  readonly kind: MailInboxErrorKind;
  readonly status?: number;

  constructor(kind: MailInboxErrorKind, message: string, status?: number) {
    super(message);
    this.name = 'MailInboxError';
    this.kind = kind;
    this.status = status;
  }
}
