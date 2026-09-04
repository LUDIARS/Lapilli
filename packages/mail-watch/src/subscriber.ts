import { PubSub } from '@google-cloud/pubsub';
import type { Message, Subscription } from '@google-cloud/pubsub';
import { decodeMailNotification } from './decode.js';
import type { MailWatchState, MailWatchSubscriberOptions } from './types.js';

const DEFAULT_RECONNECT_DELAY_SEC = 30;
const MAX_RECONNECT_DELAY_SEC = 300;
const DEFAULT_MAX_OUTSTANDING_MESSAGES = 10;

export class MailWatchSubscriber {
  private readonly stateValue: MailWatchState = { connected: false, lastMessageAt: null, lastError: null, receivedCount: 0, reconnectCount: 0 };
  private client: PubSub | undefined;
  private subscription: Subscription | undefined;
  private reconnectTimer: ReturnType<typeof setTimeout> | undefined;
  private reconnectTask: Promise<void> | undefined;
  private running = false;

  // @implements SPEC-MAIL-WATCH-001
  // @implements SPEC-MAIL-WATCH-002
  // @implements SPEC-MAIL-WATCH-003
  // @implements SPEC-MAIL-WATCH-004
  constructor(private readonly opts: MailWatchSubscriberOptions) {
    if (opts.subscriptionName === '') throw new RangeError('subscriptionName must not be empty');
    if (opts.credentials.client_email === '' || opts.credentials.private_key === '') throw new RangeError('Pub/Sub credentials must not be empty');
    if (opts.reconnectDelaySec !== undefined && (!Number.isFinite(opts.reconnectDelaySec) || opts.reconnectDelaySec <= 0)) throw new RangeError('reconnectDelaySec must be a positive finite number');
    if (opts.maxOutstandingMessages !== undefined && (!Number.isFinite(opts.maxOutstandingMessages) || opts.maxOutstandingMessages < 1)) throw new RangeError('maxOutstandingMessages must be a positive finite number');
  }

  // @implements SPEC-MAIL-WATCH-003
  get state(): MailWatchState { return { ...this.stateValue }; }

  // @implements SPEC-MAIL-WATCH-002
  start(): void {
    this.running = true;
    if (!this.subscription && !this.reconnectTimer) this.connect();
  }

  // @implements SPEC-MAIL-WATCH-002
  async stop(): Promise<void> {
    this.running = false;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = undefined;
    }
    const reconnectTask = this.reconnectTask;
    await this.closeSubscription();
    if (reconnectTask) await reconnectTask;
  }

  // @implements SPEC-MAIL-WATCH-002
  private connect(): void {
    if (!this.running || this.subscription) return;
    const client = new PubSub({ credentials: this.opts.credentials });
    const subscription = client.subscription(this.opts.subscriptionName, { flowControl: { maxMessages: Math.floor(this.opts.maxOutstandingMessages ?? DEFAULT_MAX_OUTSTANDING_MESSAGES) } });
    this.client = client;
    this.subscription = subscription;
    // @implements SPEC-MAIL-WATCH-001
    subscription.on('message', (message) => { void this.handleMessage(message); });
    // @implements SPEC-MAIL-WATCH-002
    subscription.on('error', (error) => {
      if (subscription !== this.subscription || this.reconnectTask) return;
      this.reconnectTask = this.handleTerminalError(error).finally(() => { this.reconnectTask = undefined; });
    });
    this.stateValue.connected = true;
  }

  // @implements SPEC-MAIL-WATCH-001
  // @implements SPEC-MAIL-WATCH-003
  // @implements SPEC-MAIL-WATCH-004
  private async handleMessage(message: Message): Promise<void> {
    let notification;
    try {
      notification = decodeMailNotification(message.data);
    } catch (error) {
      this.recordError(asError(error));
      message.ack();
      return;
    }
    this.stateValue.lastMessageAt = new Date();
    this.stateValue.receivedCount += 1;
    try {
      await this.opts.onNotification(notification);
      message.ack();
    } catch (error) {
      this.recordError(asError(error));
      message.nack();
    }
  }

  // @implements SPEC-MAIL-WATCH-002
  // @implements SPEC-MAIL-WATCH-003
  private async handleTerminalError(error: Error): Promise<void> {
    this.recordError(error);
    if (!this.running) return;
    this.stateValue.reconnectCount += 1;
    try {
      await this.closeSubscription();
    } catch (closeError) {
      this.recordError(asError(closeError));
    }
    if (!this.running) return;
    const delayMs = Math.min(this.baseReconnectDelayMs() * 2 ** (this.stateValue.reconnectCount - 1), MAX_RECONNECT_DELAY_SEC * 1000);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = undefined;
      this.connect();
    }, delayMs);
  }

  // @implements SPEC-MAIL-WATCH-002
  private async closeSubscription(): Promise<void> {
    const subscription = this.subscription;
    const client = this.client;
    this.subscription = undefined;
    this.client = undefined;
    this.stateValue.connected = false;
    try {
      if (subscription) await subscription.close();
    } finally {
      if (client) await client.close();
    }
  }

  // @implements SPEC-MAIL-WATCH-002
  private baseReconnectDelayMs(): number {
    return (this.opts.reconnectDelaySec ?? DEFAULT_RECONNECT_DELAY_SEC) * 1000;
  }

  // @implements SPEC-MAIL-WATCH-003
  private recordError(error: Error): void {
    this.stateValue.lastError = error.message;
    try {
      this.opts.onError?.(error);
    } catch {
      // Error reporting hooks are best-effort and must not alter ack/nack or reconnect decisions.
    }
  }
}

// @implements SPEC-MAIL-WATCH-003
// @implements SPEC-MAIL-WATCH-004
function asError(value: unknown): Error { return value instanceof Error ? value : new Error(String(value)); }
