import { spawn as spawnProcess, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync } from 'node:fs';
import { resolveExecutable } from './executable.js';
import { resolveModelArgs } from './models.js';
import { subscriptionEnvironment } from './environment.js';
export { DEFAULT_MODELS, resolveModel } from './models.js';
export { subscriptionEnvironment } from './environment.js';

/**
 * Launch a single CLI job. The caller owns streams, its deadline, cancellation and close.
 * This preserves streaming/JSON/vision contracts while centralizing launch policy.
 * No retries: a lost response must not cause an agentic job to execute twice.
 */
export function prepareOneShot(file, args, options = {}) {
  let argv = [...args];
  // Migrate existing explicit cmd.exe wrappers to the real CLI, without executing shell text.
  if (/^(?:.*[/\\])?cmd(?:\.exe)?$/i.test(file)) {
    const marker = argv.findIndex(arg => arg.toLowerCase() === '/c');
    if (marker < 0 || !argv[marker + 1] || /\s/.test(argv[marker + 1])) throw new Error('Unsupported CLI shell wrapper');
    file = argv[marker + 1];
    argv = argv.slice(marker + 2);
  }
  const provider = argv.includes('exec') ? 'codex' : argv.includes('-p') || argv.includes('--print') ? 'claude' : null;
  if (!provider) throw new Error('One-shot launch requires claude print mode or codex exec');
  const env = subscriptionEnvironment(options.env ?? process.env);
  if (provider === 'claude' && process.platform === 'win32' && !env.CLAUDE_CODE_GIT_BASH_PATH) {
    const bash = ['C:/Program Files/Git/bin/bash.exe', 'C:/Program Files (x86)/Git/bin/bash.exe'].find(existsSync);
    if (bash) env.CLAUDE_CODE_GIT_BASH_PATH = bash;
  }
  const cwd = options.cwd ?? process.cwd();
  const command = resolveExecutable(file, env, cwd);
  return { file: command.file, args: [...command.prefix, ...resolveModelArgs(argv, provider, env)],
    options: { ...options, cwd, env, shell: false, windowsHide: true } };
}

export function spawnOneShot(file, args, options = {}) {
  const launch = prepareOneShot(file, args, options);
  const child = spawnProcess(launch.file, launch.args, launch.options);
  // EPIPE is expected when authentication/startup fails before consuming the prompt.
  // The caller still observes the child's error/close and its exit status.
  child.stdin?.on('error', () => {});
  return child;
}

export function execFileOneShot(file, args, options, callback) {
  if (typeof options === 'function') { callback = options; options = {}; }
  const launch = prepareOneShot(file, args, options);
  return execFile(launch.file, launch.args, launch.options, callback);
}

execFileOneShot[promisify.custom] = (file, args, options) => new Promise((resolve, reject) => {
  execFileOneShot(file, args, options, (error, stdout, stderr) => {
    if (error) { Object.assign(error, { stdout, stderr }); reject(error); }
    else resolve({ stdout, stderr });
  });
});
