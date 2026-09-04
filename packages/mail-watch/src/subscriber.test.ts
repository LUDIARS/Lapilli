import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => {
  class SubscriptionMock {
    readonly handlers = new Map<string, (value: unknown) => void>();
    readonly close = vi.fn<() => Promise<void>>(async () => undefined);
    on(event: string, handler: (value: unknown) => void): this { this.handlers.set(event, handler); return this; }
    emit(event: string, value: unknown): void { this.handlers.get(event)?.(value); }
  }
  const subscriptions: SubscriptionMock[] = [];
  const clients: Array<{ close: ReturnType<typeof vi.fn> }> = [];
  const PubSub = vi.fn(function () {
    const client = { close: vi.fn(async () => undefined), subscription: vi.fn(() => { const subscription = new SubscriptionMock(); subscriptions.push(subscription); return subscription; }) };
    clients.push(client);
    return client;
  });
  return { PubSub, clients, subscriptions };
});

vi.mock('@google-cloud/pubsub', () => ({ PubSub: mocks.PubSub }));

import { MailWatchSubscriber } from './subscriber.js';

const credentials = { client_email: 'watch@example.test', private_key: 'key' };

beforeEach(() => { mocks.clients.splice(0); mocks.subscriptions.splice(0); mocks.PubSub.mockClear(); });

describe('MailWatchSubscriber', () => {
  it.each([0, -1, Number.NaN, Number.POSITIVE_INFINITY])('rejects invalid reconnect delay %s', (reconnectDelaySec) => {
    expect(() => new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', reconnectDelaySec, onNotification: async () => undefined })).toThrow(RangeError);
  });

  it('acks only after notification processing resolves and nacks processing failures', async () => {
    let resolveNotification: (() => void) | undefined;
    const subscriber = new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', onNotification: () => new Promise<void>((resolve) => { resolveNotification = resolve; }) });
    subscriber.start();
    const message = { data: Buffer.from(JSON.stringify({ emailAddress: 'mail@example.test', historyId: '11' })), ack: vi.fn(), nack: vi.fn() };
    mocks.subscriptions[0].emit('message', message);
    expect(message.ack).not.toHaveBeenCalled();
    resolveNotification?.();
    await vi.waitFor(() => expect(message.ack).toHaveBeenCalledOnce());
    expect(message.nack).not.toHaveBeenCalled();
    expect(subscriber.state).toMatchObject({ connected: true, receivedCount: 1 });
    expect(subscriber.state.lastMessageAt).toBeInstanceOf(Date);
    await subscriber.stop();
  });

  it('nacks a valid notification when its handler rejects', async () => {
    const subscriber = new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', onNotification: async () => { throw new Error('downstream unavailable'); }, onError: () => { throw new Error('reporter failed'); } });
    subscriber.start();
    const message = { data: Buffer.from(JSON.stringify({ emailAddress: 'mail@example.test', historyId: '12' })), ack: vi.fn(), nack: vi.fn() };
    mocks.subscriptions[0].emit('message', message);
    await vi.waitFor(() => expect(message.nack).toHaveBeenCalledOnce());
    expect(message.ack).not.toHaveBeenCalled();
    await subscriber.stop();
  });

  it('reconnects after a terminal error and exposes reconnect state', async () => {
    vi.useFakeTimers();
    const subscriber = new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', reconnectDelaySec: 1, onNotification: async () => undefined });
    subscriber.start();
    mocks.subscriptions[0].emit('error', new Error('stream closed'));
    await Promise.resolve();
    await Promise.resolve();
    expect(mocks.subscriptions[0].close).toHaveBeenCalledOnce();
    expect(subscriber.state).toMatchObject({ connected: false, reconnectCount: 1, lastError: 'stream closed' });
    await vi.advanceTimersByTimeAsync(1000);
    expect(mocks.subscriptions).toHaveLength(2);
    expect(subscriber.state.connected).toBe(true);
    await subscriber.stop();
    expect(mocks.clients.every((client) => client.close.mock.calls.length === 1)).toBe(true);
    vi.useRealTimers();
  });

  it('coalesces overlapping terminal errors into one reconnect', async () => {
    vi.useFakeTimers();
    let releaseClose: (() => void) | undefined;
    const subscriber = new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', reconnectDelaySec: 1, onNotification: async () => undefined, onError: () => { throw new Error('reporter failed'); } });
    subscriber.start();
    mocks.subscriptions[0].close.mockImplementationOnce(() => new Promise<void>((resolve) => { releaseClose = resolve; }));
    mocks.subscriptions[0].emit('error', new Error('stream closed'));
    mocks.subscriptions[0].emit('error', new Error('duplicate stream error'));
    expect(subscriber.state.reconnectCount).toBe(1);
    releaseClose?.();
    await Promise.resolve();
    await Promise.resolve();
    await vi.advanceTimersByTimeAsync(1000);
    expect(mocks.subscriptions).toHaveLength(2);
    await subscriber.stop();
    vi.useRealTimers();
  });

  it('acks malformed payloads once without retrying them', async () => {
    const subscriber = new MailWatchSubscriber({ credentials, subscriptionName: 'projects/test/subscriptions/mail', onNotification: async () => undefined, onError: () => { throw new Error('reporter failed'); } });
    subscriber.start();
    const message = { data: Buffer.from('{not-json'), ack: vi.fn(), nack: vi.fn() };
    mocks.subscriptions[0].emit('message', message);
    await vi.waitFor(() => expect(message.ack).toHaveBeenCalledOnce());
    expect(message.nack).not.toHaveBeenCalled();
    expect(subscriber.state).toMatchObject({ receivedCount: 0, lastError: 'Pub/Sub message payload was not valid JSON' });
    await subscriber.stop();
  });
});
