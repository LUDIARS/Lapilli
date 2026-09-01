export interface MailAttachment {
  filename: string;
  mimeType: string;
  size: number;
  data?: Buffer;
  attachmentId: string;
}

export interface MailMessage {
  id: string;
  threadId: string;
  from: { name?: string; address: string };
  to: string[];
  subject: string;
  date: Date;
  text: string;
  html?: string;
  snippet: string;
  labelIds: string[];
  attachments: MailAttachment[];
  headers: Record<string, string>;
}

export interface SearchOptions {
  maxResults?: number;
  newerThanEpochSec?: number;
  loadAttachments?: boolean;
  maxAttachmentBytes?: number;
}

export interface MailSource {
  search(query: string, opts?: SearchOptions): Promise<MailMessage[]>;
  get(id: string, opts?: Pick<SearchOptions, 'loadAttachments' | 'maxAttachmentBytes'>): Promise<MailMessage | null>;
  loadAttachment(messageId: string, attachmentId: string): Promise<Buffer>;
}

export interface AccessTokenProvider {
  getAccessToken(): Promise<string>;
}

export interface OAuthRefreshCredentials {
  clientId: string;
  clientSecret: string;
  refreshToken: string;
}

export interface GmailSourceOptions {
  auth: AccessTokenProvider;
  fetchImpl?: typeof fetch;
  userId?: string;
  baseUrl?: string;
}

export interface GmailHeader {
  name?: string;
  value?: string;
}

export interface GmailMessagePartBody {
  data?: string;
  attachmentId?: string;
  size?: number;
}

export interface GmailMessagePayload {
  mimeType?: string;
  filename?: string;
  headers?: GmailHeader[];
  body?: GmailMessagePartBody;
  parts?: GmailMessagePayload[];
}

export interface ParsedPayload {
  text: string;
  html?: string;
  attachments: MailAttachment[];
}
