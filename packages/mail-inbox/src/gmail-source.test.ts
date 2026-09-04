import { describe, expect, it, vi } from 'vitest';
import { MailInboxError } from './errors.js';
import { GmailSource } from './gmail-source.js';

const auth = { getAccessToken: async () => 'access-token' };

describe('GmailSource', () => {
  it('adds after and filters messages by precise internalDate', async () => {
    const fetchImpl = vi.fn<typeof fetch>()
      .mockResolvedValueOnce(jsonResponse({ messages: [{ id: 'old' }, { id: 'new' }] }))
      .mockResolvedValueOnce(messageResponse('old', 999_000))
      .mockResolvedValueOnce(messageResponse('new', 1_001_000));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await expect(source.search('in:inbox', { newerThanEpochSec: 1000 })).resolves.toMatchObject([{ id: 'new' }]);
    expect(fetchImpl.mock.calls[0][0]).toContain('q=in%3Ainbox+after%3A1000');
  });

  it('does not request attachment bodies when loadAttachments is false', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(messageResponse('mail-1', 1_001_000, { attachmentId: 'a-1', size: 4 }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    const message = await source.get('mail-1');
    expect(message?.attachments[0]).toMatchObject({ attachmentId: 'a-1' });
    expect(message?.attachments[0].data).toBeUndefined();
    expect(fetchImpl).toHaveBeenCalledTimes(1);
  });

  it('encodes a custom user ID as one URL path segment', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(messageResponse('mail-1', 1_001_000));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1', userId: '../user@example.com' });
    await source.get('mail-1');
    expect(fetchImpl.mock.calls[0][0]).toBe('https://gmail.test/v1/users/..%2Fuser%40example.com/messages/mail-1?format=full');
    expect(fetchImpl.mock.calls[0][1]).toMatchObject({ redirect: 'error' });
  });

  it('preserves request headers without allowing the access token to be overridden', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ historyId: '77' }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await source.requestJson('/users/me/profile', { headers: new Headers({ authorization: 'Bearer attacker-token', 'x-request-id': 'request-1' }) });
    const headers = new Headers(fetchImpl.mock.calls[0][1]?.headers);
    expect(headers.get('authorization')).toBe('Bearer access-token');
    expect(headers.get('x-request-id')).toBe('request-1');
  });

  it('rejects an invalid internalDate response', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(messageResponse('mail-1', Number.NaN));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await expect(source.get('mail-1')).rejects.toMatchObject<Partial<MailInboxError>>({ kind: 'invalid_response' });
  });

  it('rejects a malformed message list response', async () => {
    const source = new GmailSource({
      auth,
      fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(jsonResponse(null)),
      baseUrl: 'https://gmail.test/v1',
    });
    await expect(source.search('in:inbox')).rejects.toMatchObject<Partial<MailInboxError>>({ kind: 'invalid_response' });
  });

  it('does not expose attachment data larger than the configured limit', async () => {
    const fetchImpl = vi.fn<typeof fetch>()
      .mockResolvedValueOnce(messageResponse('mail-1', 1_001_000, { attachmentId: 'a-1', size: 4 }))
      .mockResolvedValueOnce(jsonResponse({ data: 'aGVsbG8' }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    const message = await source.get('mail-1', { loadAttachments: true, maxAttachmentBytes: 4 });
    expect(message?.attachments[0].data).toBeUndefined();
  });

  it.each([[401, 'auth'], [429, 'rate_limit']] as const)('maps %i to %s', async (status, kind) => {
    const source = new GmailSource({ auth, fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(new Response('{}', { status })) });
    await expect(source.get('mail-1')).rejects.toMatchObject<Partial<MailInboxError>>({ kind, status });
  });
});

function jsonResponse(value: unknown): Response {
  return new Response(JSON.stringify(value), { status: 200, headers: { 'content-type': 'application/json' } });
}

function messageResponse(id: string, internalDate: number, attachment?: { attachmentId: string; size: number }): Response {
  return jsonResponse({
    id, threadId: 'thread-1', internalDate: String(internalDate), snippet: 'snippet', labelIds: ['INBOX'],
    payload: {
      headers: [{ name: 'From', value: 'Sender <sender@example.com>' }, { name: 'To', value: 'to@example.com' }, { name: 'Subject', value: 'Subject' }],
      parts: [{ mimeType: 'text/plain', body: { data: 'aGVsbG8' } }, ...(attachment ? [{ mimeType: 'application/pdf', filename: 'file.pdf', body: attachment }] : [])],
    },
  });
}
