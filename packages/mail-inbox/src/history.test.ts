import { describe, expect, it, vi } from 'vitest';
import { GmailSource } from './gmail-source.js';

const auth = { getAccessToken: async () => 'access-token' };

describe('GmailSource history', () => {
  it('returns expired instead of throwing when Gmail history has expired', async () => {
    const source = new GmailSource({ auth, fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(new Response('{}', { status: 404 })), baseUrl: 'https://gmail.test/v1' });
    await expect(source.history('123')).resolves.toEqual({ changes: [], historyId: '123', expired: true });
  });

  it('collects every page and advances only after the final page', async () => {
    const fetchImpl = vi.fn<typeof fetch>()
      .mockResolvedValueOnce(jsonResponse({ historyId: '200', nextPageToken: 'next', history: [{ messagesAdded: [{ message: { id: 'one' } }] }] }))
      .mockResolvedValueOnce(jsonResponse({ historyId: '300', history: [{ labelsRemoved: [{ message: { id: 'two' } }] }] }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await expect(source.history('100')).resolves.toEqual({ changes: [{ messageId: 'one', type: 'added' }, { messageId: 'two', type: 'labelRemoved' }], historyId: '300', expired: false });
    expect(fetchImpl.mock.calls[1][0]).toContain('pageToken=next');
  });

  it('does not advance history ID when maxPages cuts pagination short', async () => {
    const source = new GmailSource({ auth, fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ historyId: '200', nextPageToken: 'next', history: [] })), baseUrl: 'https://gmail.test/v1' });
    await expect(source.history('100', { maxPages: 1 })).resolves.toMatchObject({ historyId: '100', expired: false });
  });
});

function jsonResponse(value: unknown): Response {
  return new Response(JSON.stringify(value), { status: 200, headers: { 'content-type': 'application/json' } });
}
