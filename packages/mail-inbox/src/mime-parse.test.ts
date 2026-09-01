import { describe, expect, it } from 'vitest';
import { parseGmailPayload } from './mime-parse.js';

describe('parseGmailPayload', () => {
  it('extracts multipart alternative content and attachment metadata', () => {
    const parsed = parseGmailPayload({
      mimeType: 'multipart/mixed',
      parts: [{
        mimeType: 'multipart/alternative',
        parts: [
          { mimeType: 'text/plain', body: { data: 'cGxhaW4gdGV4dA' } },
          { mimeType: 'text/html', body: { data: 'PHA-aHRtbCB0ZXh0PC9wPg' } },
        ],
      }, {
        mimeType: 'application/pdf', filename: 'invoice.pdf', body: { attachmentId: 'attachment-1', size: 42 },
      }],
    });
    expect(parsed.text).toBe('plain text');
    expect(parsed.html).toBe('<p>html text</p>');
    expect(parsed.attachments).toEqual([{ filename: 'invoice.pdf', mimeType: 'application/pdf', size: 42, attachmentId: 'attachment-1' }]);
  });

  it('derives text from HTML-only content', () => {
    const parsed = parseGmailPayload({ mimeType: 'text/html', body: { data: 'PHA-SGVsbG8gJmFtcDsgV29ybGQ8L3A-' } });
    expect(parsed.text).toBe('Hello & World');
  });
});
