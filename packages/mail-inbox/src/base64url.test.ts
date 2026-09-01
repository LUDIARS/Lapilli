import { describe, expect, it } from 'vitest';
import { decodeBase64Url } from './base64url.js';

describe('decodeBase64Url', () => {
  it('decodes URL-safe base64 without padding', () => {
    expect(decodeBase64Url('44GT44KT44Gr44Gh44Gv').toString('utf8')).toBe('こんにちは');
  });
});
