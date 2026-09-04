import type { MailNotification } from './types.js';

// @implements SPEC-MAIL-WATCH-004
export function decodeMailNotification(data: Buffer): MailNotification {
  let value: unknown;
  try {
    value = JSON.parse(data.toString('utf8'));
  } catch {
    throw new Error('Pub/Sub message payload was not valid JSON');
  }
  if (!isRecord(value) || typeof value.emailAddress !== 'string' || value.emailAddress === '' || typeof value.historyId !== 'string' || value.historyId === '') {
    throw new Error('Pub/Sub message payload was not a Gmail notification');
  }
  return { emailAddress: value.emailAddress, historyId: value.historyId };
}

// @implements SPEC-MAIL-WATCH-004
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
