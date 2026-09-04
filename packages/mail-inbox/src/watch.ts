import { MailInboxError } from './errors.js';
import type { GmailJsonRequester } from './history.js';
import type { MailWatchOptions, MailWatchRegistration } from './types.js';

// @implements SPEC-MAIL-INBOX-007
export async function registerWatch(requester: GmailJsonRequester, encodedUserId: string, opts: MailWatchOptions): Promise<MailWatchRegistration> {
  if (opts.topicName === '') throw new RangeError('topicName must not be empty');
  const value = await requester.requestJson<unknown>(`/users/${encodedUserId}/watch`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ topicName: opts.topicName, labelIds: opts.labelIds ?? [], labelFilterBehavior: 'include' }) });
  if (!isRecord(value) || typeof value.historyId !== 'string' || value.historyId === '' || typeof value.expiration !== 'string' || !/^\d+$/.test(value.expiration)) throw new MailInboxError('invalid_response', 'Gmail watch response was incomplete');
  const expirationMilliseconds = Number(value.expiration);
  if (!Number.isSafeInteger(expirationMilliseconds) || expirationMilliseconds <= 0) throw new MailInboxError('invalid_response', 'Gmail watch expiration was invalid');
  const expiration = new Date(expirationMilliseconds);
  if (Number.isNaN(expiration.getTime())) throw new MailInboxError('invalid_response', 'Gmail watch expiration was invalid');
  return { historyId: value.historyId, expiration };
}

// @implements SPEC-MAIL-INBOX-008
export async function stopWatch(requester: GmailJsonRequester, encodedUserId: string): Promise<void> {
  await requester.requestJson(`/users/${encodedUserId}/stop`, { method: 'POST' });
}

// @implements SPEC-MAIL-INBOX-008
export async function fetchCurrentHistoryId(requester: GmailJsonRequester, encodedUserId: string): Promise<string> {
  const value = await requester.requestJson<unknown>(`/users/${encodedUserId}/profile`);
  if (!isRecord(value) || typeof value.historyId !== 'string' || value.historyId === '') throw new MailInboxError('invalid_response', 'Gmail profile response was incomplete');
  return value.historyId;
}

// @implements SPEC-MAIL-INBOX-007
// @implements SPEC-MAIL-INBOX-008
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
