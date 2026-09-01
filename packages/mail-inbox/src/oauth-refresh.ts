import { MailInboxError } from './errors.js';
import type { AccessTokenProvider, OAuthRefreshCredentials } from './types.js';

const DEFAULT_TOKEN_ENDPOINT = 'https://oauth2.googleapis.com/token';

// @implements SPEC-MAIL-INBOX-003
export function createRefreshTokenProvider(
  creds: OAuthRefreshCredentials,
  opts: { fetchImpl?: typeof fetch; tokenEndpoint?: string; now?: () => number } = {},
): AccessTokenProvider {
  const fetchImpl = opts.fetchImpl ?? globalThis.fetch;
  const tokenEndpoint = opts.tokenEndpoint ?? DEFAULT_TOKEN_ENDPOINT;
  const now = opts.now ?? Date.now;
  let cached: { token: string; expiresAt: number } | undefined;

  return {
    async getAccessToken(): Promise<string> {
      if (cached && cached.expiresAt > now()) return cached.token;
      let response: Response;
      try {
        response = await fetchImpl(tokenEndpoint, {
          method: 'POST',
          headers: { 'content-type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({
            client_id: creds.clientId,
            client_secret: creds.clientSecret,
            refresh_token: creds.refreshToken,
            grant_type: 'refresh_token',
          }),
          redirect: 'error',
        });
      } catch {
        throw new MailInboxError('auth', 'OAuth token refresh failed');
      }
      if (!response.ok) throw new MailInboxError('auth', 'OAuth token refresh was rejected', response.status);
      let body: unknown;
      try {
        body = await response.json();
      } catch {
        throw new MailInboxError('auth', 'OAuth token response was invalid', response.status);
      }
      if (!isTokenResponse(body)) throw new MailInboxError('auth', 'OAuth token response was invalid');
      cached = { token: body.access_token, expiresAt: now() + Math.max(0, body.expires_in * 1000 - 60_000) };
      return cached.token;
    },
  };
}

// @implements SPEC-MAIL-INBOX-003
function isTokenResponse(value: unknown): value is { access_token: string; expires_in: number } {
  if (typeof value !== 'object' || value === null) return false;
  const { access_token: accessToken, expires_in: expiresIn } = value as Record<string, unknown>;
  return typeof accessToken === 'string' && accessToken !== ''
    && typeof expiresIn === 'number' && Number.isFinite(expiresIn) && expiresIn > 0;
}
