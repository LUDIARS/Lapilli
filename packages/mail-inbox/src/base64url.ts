// @implements SPEC-MAIL-INBOX-002
export function decodeBase64Url(value: string): Buffer {
  const base64 = value.replace(/-/g, '+').replace(/_/g, '/');
  return Buffer.from(base64, 'base64');
}
