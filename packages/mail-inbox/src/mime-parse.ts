import { decodeBase64Url } from './base64url.js';
import type { GmailMessagePayload, MailAttachment, ParsedPayload } from './types.js';

// @implements SPEC-MAIL-INBOX-002
// @implements SPEC-MAIL-INBOX-004
export function parseGmailPayload(payload: GmailMessagePayload): ParsedPayload {
  const result: ParsedPayload = { text: '', attachments: [] };
  visitPart(payload, result);
  if (!result.text && result.html) {
    result.text = htmlToText(result.html);
  }
  return result;
}

// @implements SPEC-MAIL-INBOX-002
function visitPart(part: GmailMessagePayload, result: ParsedPayload): void {
  const body = part.body;
  if (body?.attachmentId) {
    result.attachments.push({
      filename: part.filename ?? '',
      mimeType: part.mimeType ?? 'application/octet-stream',
      size: body.size ?? 0,
      attachmentId: body.attachmentId,
    });
  } else if (body?.data) {
    const value = decodeBase64Url(body.data).toString('utf8');
    if (part.mimeType === 'text/plain' && !result.text) result.text = value;
    if (part.mimeType === 'text/html' && !result.html) result.html = value;
  }
  for (const child of part.parts ?? []) visitPart(child, result);
}

// @implements SPEC-MAIL-INBOX-004
function htmlToText(html: string): string {
  return decodeHtmlEntities(
    html
      .replace(/<br\s*\/?\s*>/gi, '\n')
      .replace(/<\/(p|div|li|tr|h[1-6])\s*>/gi, '\n')
      .replace(/<[^>]*>/g, '')
      .replace(/\s+\n/g, '\n')
      .trim(),
  );
}

// @implements SPEC-MAIL-INBOX-004
function decodeHtmlEntities(value: string): string {
  return value.replace(/&(?:amp|lt|gt|quot|#39);/g, (entity) => ({
    '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"', '&#39;': "'",
  })[entity] ?? entity);
}
