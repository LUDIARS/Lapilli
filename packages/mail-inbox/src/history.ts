import { MailInboxError } from './errors.js';
import type { MailHistoryChange, MailHistoryOptions, MailHistoryPage } from './types.js';

const DEFAULT_MAX_PAGES = 10;

export interface GmailJsonRequester {
  requestJson<T>(path: string, init?: RequestInit): Promise<T>;
}

// @implements SPEC-MAIL-INBOX-005
// @implements SPEC-MAIL-INBOX-006
export async function fetchHistory(requester: GmailJsonRequester, encodedUserId: string, startHistoryId: string, opts: MailHistoryOptions = {}): Promise<MailHistoryPage> {
  if (startHistoryId === '') throw new RangeError('startHistoryId must not be empty');
  const maxPages = normalizeMaxPages(opts.maxPages);
  let pageToken: string | undefined;
  const changes: MailHistoryChange[] = [];
  for (let page = 0; page < maxPages; page += 1) {
    const params = new URLSearchParams({ startHistoryId });
    for (const labelId of opts.labelIds ?? []) params.append('labelId', labelId);
    for (const historyType of opts.historyTypes ?? ['messageAdded']) params.append('historyTypes', historyType);
    if (pageToken) params.set('pageToken', pageToken);
    let response: unknown;
    try {
      response = await requester.requestJson(`/users/${encodedUserId}/history?${params}`);
    } catch (error) {
      if (error instanceof MailInboxError && error.kind === 'not_found') return { changes: [], historyId: startHistoryId, expired: true };
      throw error;
    }
    const parsed = parseHistoryResponse(response);
    changes.push(...parsed.changes);
    if (!parsed.nextPageToken) return { changes, historyId: parsed.historyId, expired: false };
    pageToken = parsed.nextPageToken;
  }
  return { changes, historyId: startHistoryId, expired: false };
}

// @implements SPEC-MAIL-INBOX-006
export function parseHistoryResponse(value: unknown): { changes: MailHistoryChange[]; historyId: string; nextPageToken?: string } {
  if (!isRecord(value) || typeof value.historyId !== 'string' || value.historyId === '') throw new MailInboxError('invalid_response', 'Gmail history response was incomplete');
  if (value.nextPageToken !== undefined && (typeof value.nextPageToken !== 'string' || value.nextPageToken === '')) throw new MailInboxError('invalid_response', 'Gmail history response page token was invalid');
  if (value.history !== undefined && !Array.isArray(value.history)) throw new MailInboxError('invalid_response', 'Gmail history response was invalid');
  return { changes: (value.history ?? []).flatMap(parseHistoryRecord), historyId: value.historyId, ...(value.nextPageToken ? { nextPageToken: value.nextPageToken } : {}) };
}

// @implements SPEC-MAIL-INBOX-006
function parseHistoryRecord(value: unknown): MailHistoryChange[] {
  if (!isRecord(value)) return [];
  return [...parseChangeList(value.messagesAdded, 'added'), ...parseChangeList(value.messagesDeleted, 'deleted'), ...parseChangeList(value.labelsAdded, 'labelAdded'), ...parseChangeList(value.labelsRemoved, 'labelRemoved')];
}

// @implements SPEC-MAIL-INBOX-006
function parseChangeList(value: unknown, type: MailHistoryChange['type']): MailHistoryChange[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((entry) => isRecord(entry) && isRecord(entry.message) && typeof entry.message.id === 'string' && entry.message.id !== '' ? [{ messageId: entry.message.id, type }] : []);
}

// @implements SPEC-MAIL-INBOX-006
function normalizeMaxPages(value: number | undefined): number {
  if (value === undefined) return DEFAULT_MAX_PAGES;
  if (!Number.isFinite(value) || value < 1) throw new RangeError('maxPages must be a positive finite number');
  return Math.floor(value);
}

// @implements SPEC-MAIL-INBOX-006
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
