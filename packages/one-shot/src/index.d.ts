import { spawn, execFile, type SpawnOptions } from 'node:child_process';
export const spawnOneShot: typeof spawn;
export const execFileOneShot: typeof execFile;
export function prepareOneShot(file: string, args: readonly string[], options?: SpawnOptions): { file: string; args: string[]; options: SpawnOptions };
export const DEFAULT_MODELS: Readonly<Record<'opus' | 'sonnet' | 'haiku' | 'fable' | 'astra' | 'sol' | 'luna', string>>;
export function resolveModel(roleOrId: string | undefined, provider: 'claude' | 'codex', env?: NodeJS.ProcessEnv): string;
export function subscriptionEnvironment(source?: NodeJS.ProcessEnv): NodeJS.ProcessEnv;
