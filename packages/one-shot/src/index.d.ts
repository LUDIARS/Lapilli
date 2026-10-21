import { spawn, execFile, type SpawnOptions } from 'node:child_process';
export const spawnOneShot: typeof spawn;
export const execFileOneShot: typeof execFile;
export function prepareOneShot(file: string, args: readonly string[], options?: SpawnOptions): { file: string; args: string[]; options: SpawnOptions };
export const DEFAULT_MODELS: Readonly<Record<'opus' | 'sonnet' | 'haiku' | 'fable' | 'astra' | 'sol' | 'luna', string>>;
export function resolveModel(roleOrId: string | undefined, provider: 'claude' | 'codex', env?: NodeJS.ProcessEnv): string;
export function subscriptionEnvironment(source?: NodeJS.ProcessEnv): NodeJS.ProcessEnv;
export type OneShotProvider = 'claude' | 'codex';
export type OneShotTier = 'ultra' | 'high' | 'medium' | 'low';
export interface QuotaWindow { usedPercent: number | null; resetsAt: number | null }
export interface QuotaObservation {
  observedAt: number;
  windows?: readonly QuotaWindow[];
  modelWindows?: Readonly<Record<string, readonly QuotaWindow[]>>;
}
export type OneShotAvailability = Partial<Record<OneShotProvider, QuotaObservation>>;
export interface ModelSelection { provider: OneShotProvider; model: string; reason: string; remainingPercent?: number }
export interface ModelSelectionOptions {
  requested?: { provider: OneShotProvider; model: string };
  tier?: OneShotTier;
  availability?: OneShotAvailability;
  env?: NodeJS.ProcessEnv;
  now?: number;
  maxAgeMs?: number;
}
export function selectAvailableModel(options: ModelSelectionOptions): ModelSelection;
export interface AvailableOneShotOptions<T> extends Omit<ModelSelectionOptions, 'availability'> {
  signal?: AbortSignal;
  readAvailability?: (options: { signal?: AbortSignal }) => Promise<OneShotAvailability>;
  executors: Partial<Record<OneShotProvider, (prompt: string, options: ModelSelection & { signal?: AbortSignal }) => Promise<T>>>;
}
export function runAvailableOneShot<T>(prompt: string, options: AvailableOneShotOptions<T>): Promise<{ selection: ModelSelection; result: T }>;
