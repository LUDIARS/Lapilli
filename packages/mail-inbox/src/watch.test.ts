import { describe, expect, it, vi } from 'vitest';
import { GmailSource } from './gmail-source.js';

const auth = { getAccessToken: async () => 'access-token' };

describe('GmailSource watch', () => {
  it('normalizes watch expiration and uses an include label filter', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ historyId: '44', expiration: '1735689600000' }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await expect(source.watch({ topicName: 'projects/example/topics/mail', labelIds: ['INBOX'] })).resolves.toEqual({ historyId: '44', expiration: new Date(1735689600000) });
    expect(fetchImpl.mock.calls[0][0]).toBe('https://gmail.test/v1/users/me/watch');
    expect(fetchImpl.mock.calls[0][1]).toMatchObject({ method: 'POST', body: JSON.stringify({ topicName: 'projects/example/topics/mail', labelIds: ['INBOX'], labelFilterBehavior: 'include' }) });
  });

  it('stops a watch and reads the current history ID', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValueOnce(jsonResponse({})).mockResolvedValueOnce(jsonResponse({ historyId: '77' }));
    const source = new GmailSource({ auth, fetchImpl, baseUrl: 'https://gmail.test/v1' });
    await source.stopWatch();
    await expect(source.currentHistoryId()).resolves.toBe('77');
    expect(fetchImpl.mock.calls[0][0]).toBe('https://gmail.test/v1/users/me/stop');
    expect(fetchImpl.mock.calls[1][0]).toBe('https://gmail.test/v1/users/me/profile');
  });

  it.each(['', ' ', '-1', '1.5', '9007199254740991', '9007199254740992'])('rejects invalid watch expiration %j', async (expiration) => {
    const source = new GmailSource({ auth, fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ historyId: '44', expiration })), baseUrl: 'https://gmail.test/v1' });
    await expect(source.watch({ topicName: 'projects/example/topics/mail' })).rejects.toMatchObject({ kind: 'invalid_response' });
  });
});

function jsonResponse(value: unknown): Response {
  return new Response(JSON.stringify(value), { status: 200, headers: { 'content-type': 'application/json' } });
}
