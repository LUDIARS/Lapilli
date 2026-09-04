export interface MailNotification {
  emailAddress: string;
  historyId: string;
}

export interface MailWatchSubscriberOptions {
  subscriptionName: string;
  credentials: { client_email: string; private_key: string };
  onNotification: (notification: MailNotification) => Promise<void>;
  onError?: (error: Error) => void;
  reconnectDelaySec?: number;
  maxOutstandingMessages?: number;
}

export interface MailWatchState {
  connected: boolean;
  lastMessageAt: Date | null;
  lastError: string | null;
  receivedCount: number;
  reconnectCount: number;
}
