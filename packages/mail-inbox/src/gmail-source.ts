import { decodeBase64Url } from './base64url.js';
import { MailInboxError } from './errors.js';
import { parseGmailPayload } from './mime-parse.js';
import type { GmailMessagePayload, GmailSourceOptions, MailMessage, MailSource, ParsedPayload, SearchOptions } from './types.js';

const DEFAULT_BASE_URL = 'https://gmail.googleapis.com/gmail/v1';
const DEFAULT_MAX_RESULTS = 50;
const MAX_RESULTS = 500;
const DEFAULT_MAX_ATTACHMENT_BYTES = 15 * 1024 * 1024;

interface GmailMessage {
  id?: string;
  threadId?: string;
  internalDate?: string;
  snippet?: string;
  labelIds?: string[];
  payload?: GmailMessagePayload;
}

// @implements SPEC-MAIL-INBOX-001
// @implements SPEC-MAIL-INBOX-002
// @implements SPEC-MAIL-INBOX-003
export class GmailSource implements MailSource {
  private readonly auth;
  private readonly fetchImpl: typeof fetch;
  private readonly encodedUserId: string;
  private readonly baseUrl: string;

  // @implements SPEC-MAIL-INBOX-001
  // @implements SPEC-MAIL-INBOX-002
  constructor(opts: GmailSourceOptions) {
    this.auth = opts.auth;
    this.fetchImpl = opts.fetchImpl ?? globalThis.fetch;
    this.encodedUserId = encodeURIComponent(opts.userId ?? 'me');
    this.baseUrl = (opts.baseUrl ?? DEFAULT_BASE_URL).replace(/\/$/, '');
  }

  async search(query: string, opts: SearchOptions = {}): Promise<MailMessage[]> {
    const maxResults = clampMaxResults(opts.maxResults);
    const effectiveQuery = opts.newerThanEpochSec === undefined ? query : `${query} after:${opts.newerThanEpochSec}`.trim();
    const params = new URLSearchParams({ q: effectiveQuery, maxResults: String(maxResults) });
    const listing = await this.requestJson<unknown>(`/users/${this.encodedUserId}/messages?${params}`);
    const ids = parseMessageIds(listing);
    const messages = await mapWithConcurrency(ids, 5, async (id) => this.get(id, opts));
    return messages.filter((message): message is MailMessage => message !== null)
      .filter((message) => opts.newerThanEpochSec === undefined || message.date.getTime() / 1000 > opts.newerThanEpochSec);
  }

  async get(id: string, opts: Pick<SearchOptions, 'loadAttachments' | 'maxAttachmentBytes'> = {}): Promise<MailMessage | null> {
    const maxAttachmentBytes = normalizeMaxAttachmentBytes(opts.maxAttachmentBytes);
    let responseBody: unknown;
    try {
      responseBody = await this.requestJson<unknown>(`/users/${this.encodedUserId}/messages/${encodeURIComponent(id)}?format=full`);
    } catch (error) {
      if (error instanceof MailInboxError && error.kind === 'not_found') return null;
      throw error;
    }
    if (!isCompleteGmailMessage(responseBody)) {
      throw new MailInboxError('invalid_response', 'Gmail message response was incomplete');
    }
    const raw = responseBody;
    const internalDate = Number(raw.internalDate);
    if (!Number.isFinite(internalDate) || internalDate < 0) {
      throw new MailInboxError('invalid_response', 'Gmail message internalDate was invalid');
    }
    const messageId = raw.id;
    const headers = normalizeHeaders(raw.payload.headers);
    let parsed: ParsedPayload;
    try {
      parsed = parseGmailPayload(raw.payload);
    } catch {
      throw new MailInboxError('invalid_response', 'Gmail message payload was invalid');
    }
    const attachments = await Promise.all(parsed.attachments.map(async (attachment) => {
      if (!opts.loadAttachments || attachment.size > maxAttachmentBytes) return attachment;
      const data = await this.loadAttachment(messageId, attachment.attachmentId);
      return data.byteLength <= maxAttachmentBytes ? { ...attachment, data } : attachment;
    }));
    return {
      id: raw.id,
      threadId: raw.threadId,
      from: parseAddress(headers.from),
      to: parseRecipients(headers.to),
      subject: headers.subject ?? '',
      date: new Date(internalDate),
      text: parsed.text,
      ...(parsed.html ? { html: parsed.html } : {}),
      snippet: typeof raw.snippet === 'string' ? raw.snippet : '',
      labelIds: Array.isArray(raw.labelIds) ? raw.labelIds.filter((label): label is string => typeof label === 'string') : [],
      attachments,
      headers,
    };
  }

  async loadAttachment(messageId: string, attachmentId: string): Promise<Buffer> {
    const response = await this.requestJson<unknown>(
      `/users/${this.encodedUserId}/messages/${encodeURIComponent(messageId)}/attachments/${encodeURIComponent(attachmentId)}`,
    );
    if (typeof response !== 'object' || response === null) {
      throw new MailInboxError('invalid_response', 'Gmail attachment response was incomplete');
    }
    const data = (response as { data?: unknown }).data;
    if (typeof data !== 'string') throw new MailInboxError('invalid_response', 'Gmail attachment response was incomplete');
    return decodeBase64Url(data);
  }

  private async requestJson<T>(path: string): Promise<T> {
    const token = await this.auth.getAccessToken();
    let response: Response;
    try {
      response = await this.fetchImpl(`${this.baseUrl}${path}`, {
        headers: { authorization: `Bearer ${token}` },
        redirect: 'error',
      });
    } catch {
      throw new MailInboxError('network', 'Gmail API request failed');
    }
    if (!response.ok) throw await createResponseError(response);
    try {
      return await response.json() as T;
    } catch {
      throw new MailInboxError('invalid_response', 'Gmail API returned invalid JSON', response.status);
    }
  }
}

async function createResponseError(response: Response): Promise<MailInboxError> {
  if (response.status === 401) return new MailInboxError('auth', 'Gmail authentication failed', response.status);
  if (response.status === 429) return new MailInboxError('rate_limit', 'Gmail rate limit exceeded', response.status);
  let body: unknown;
  try { body = await response.json(); } catch { body = undefined; }
  if (response.status === 403 && hasRateLimitReason(body)) return new MailInboxError('rate_limit', 'Gmail rate limit exceeded', response.status);
  if (response.status === 404) return new MailInboxError('not_found', 'Gmail message was not found', response.status);
  return new MailInboxError('invalid_response', `Gmail API returned ${response.status}`, response.status);
}

function hasRateLimitReason(body: unknown): boolean {
  if (typeof body !== 'object' || body === null) return false;
  const error = (body as { error?: unknown }).error;
  if (typeof error !== 'object' || error === null) return false;
  const errors = (error as { errors?: unknown }).errors;
  return Array.isArray(errors) && errors.some((item) => typeof item === 'object' && item !== null
    && (item as { reason?: unknown }).reason === 'rateLimitExceeded');
}

function parseMessageIds(value: unknown): string[] {
  if (typeof value !== 'object' || value === null) {
    throw new MailInboxError('invalid_response', 'Gmail message list response was invalid');
  }
  const messages = (value as { messages?: unknown }).messages;
  if (messages === undefined) return [];
  if (!Array.isArray(messages)) {
    throw new MailInboxError('invalid_response', 'Gmail message list response was invalid');
  }
  return messages.flatMap((message) => {
    if (typeof message !== 'object' || message === null) return [];
    const id = (message as { id?: unknown }).id;
    return typeof id === 'string' && id !== '' ? [id] : [];
  });
}

function isCompleteGmailMessage(value: unknown): value is GmailMessage & {
  id: string;
  threadId: string;
  internalDate: string;
  payload: GmailMessagePayload;
} {
  if (typeof value !== 'object' || value === null) return false;
  const message = value as Record<string, unknown>;
  return typeof message.id === 'string' && message.id !== ''
    && typeof message.threadId === 'string' && message.threadId !== ''
    && typeof message.internalDate === 'string' && message.internalDate !== ''
    && typeof message.payload === 'object' && message.payload !== null && !Array.isArray(message.payload);
}

function normalizeHeaders(headers: GmailMessagePayload['headers']): Record<string, string> {
  return Object.fromEntries((Array.isArray(headers) ? headers : []).flatMap((header) =>
    typeof header === 'object' && header !== null
      && typeof header.name === 'string' && typeof header.value === 'string'
      ? [[header.name.toLowerCase(), header.value]]
      : []));
}

function parseAddress(value: string | undefined): { name?: string; address: string } {
  if (!value) return { address: '' };
  const match = /^(.*?)\s*<([^>]+)>$/.exec(value.trim());
  if (!match) return { address: value.trim() };
  const name = match[1].trim().replace(/^"|"$/g, '');
  return name ? { name, address: match[2].trim() } : { address: match[2].trim() };
}

function parseRecipients(value: string | undefined): string[] {
  return value ? value.split(',').map((recipient) => parseAddress(recipient).address).filter(Boolean) : [];
}

function clampMaxResults(value: number | undefined): number {
  if (value !== undefined && !Number.isFinite(value)) throw new RangeError('maxResults must be finite');
  return Math.min(MAX_RESULTS, Math.max(1, Math.floor(value ?? DEFAULT_MAX_RESULTS)));
}

function normalizeMaxAttachmentBytes(value: number | undefined): number {
  if (value === undefined) return DEFAULT_MAX_ATTACHMENT_BYTES;
  if (!Number.isFinite(value) || value < 0) throw new RangeError('maxAttachmentBytes must be a non-negative finite number');
  return Math.floor(value);
}

async function mapWithConcurrency<T, U>(items: readonly T[], limit: number, mapper: (item: T) => Promise<U>): Promise<U[]> {
  const result: U[] = [];
  for (let index = 0; index < items.length; index += limit) {
    result.push(...await Promise.all(items.slice(index, index + limit).map(mapper)));
  }
  return result;
}
