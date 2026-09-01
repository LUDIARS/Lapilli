import { describe, expect, it, vi } from 'vitest';
import { MailInboxError } from './errors.js';
import { createRefreshTokenProvider } from './oauth-refresh.js';

describe('createRefreshTokenProvider', () => {
  it('caches an access token until 60 seconds before expiry', async () => {
    let now = 1_000;
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(new Response(JSON.stringify({ access_token: 'token-1', expires_in: 3600 })));
    const provider = createRefreshTokenProvider({ clientId: 'id', clientSecret: 'secret', refreshToken: 'refresh' }, { fetchImpl, now: () => now });
    await expect(provider.getAccessToken()).resolves.toBe('token-1');
    now += 1_000;
    await expect(provider.getAccessToken()).resolves.toBe('token-1');
    expect(fetchImpl).toHaveBeenCalledTimes(1);
    expect(fetchImpl.mock.calls[0][1]).toMatchObject({ redirect: 'error' });
  });

  it('maps a rejected refresh to an auth error', async () => {
    const provider = createRefreshTokenProvider(
      { clientId: 'id', clientSecret: 'secret', refreshToken: 'refresh' },
      { fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(new Response('{}', { status: 400 })) },
    );
    await expect(provider.getAccessToken()).rejects.toMatchObject<Partial<MailInboxError>>({ kind: 'auth', status: 400 });
  });

  it('maps malformed successful responses to an auth error', async () => {
    const provider = createRefreshTokenProvider(
      { clientId: 'id', clientSecret: 'secret', refreshToken: 'refresh' },
      { fetchImpl: vi.fn<typeof fetch>().mockResolvedValue(new Response('not-json', { status: 200 })) },
    );
    await expect(provider.getAccessToken()).rejects.toMatchObject<Partial<MailInboxError>>({ kind: 'auth', status: 200 });
  });
});
