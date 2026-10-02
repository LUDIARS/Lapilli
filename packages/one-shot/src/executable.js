import { existsSync } from 'node:fs';
import { delimiter, dirname, isAbsolute, join, resolve } from 'node:path';

/** Resolve native binaries or the known npm JS entry point without cmd.exe interpolation. */
export function resolveExecutable(file, env, cwd = process.cwd()) {
  if (process.platform !== 'win32') return { file, prefix: [] };
  const dirs = isAbsolute(file) || /[/\\]/.test(file)
    ? [dirname(resolve(cwd, file))]
    : (env.PATH || env.Path || '').split(delimiter).filter(Boolean);
  const name = file.split(/[/\\]/).at(-1).replace(/\.(cmd|exe)$/i, '');
  for (const dir of dirs) {
    const native = join(dir, `${name}.exe`);
    if (existsSync(native)) return { file: native, prefix: [] };
    if (name === 'codex') {
      const triple = process.arch === 'arm64' ? 'aarch64-pc-windows-msvc' : 'x86_64-pc-windows-msvc';
      const packages = ['codex', process.arch === 'arm64' ? 'codex-win32-arm64' : 'codex-win32-x64'];
      for (const pkg of packages) {
        const binary = join(dir, 'node_modules', '@openai', pkg, 'vendor', triple, 'codex', 'codex.exe');
        if (existsSync(binary)) return { file: binary, prefix: [] };
      }
    }
    const entry = name === 'codex' ? join(dir, 'node_modules', '@openai', 'codex', 'bin', 'codex.js')
      : name === 'claude' ? join(dir, 'node_modules', '@anthropic-ai', 'claude-code', 'cli.js') : null;
    if (entry && existsSync(entry)) return { file: process.execPath, prefix: [entry] };
  }
  if (/\.(cmd|bat)$/i.test(file)) throw new Error('One-shot launch requires a native executable or a known npm CLI entry point');
  // Preserve explicit native wrappers (including node-based fixtures); spawn reports ENOENT.
  return { file, prefix: [] };
}
