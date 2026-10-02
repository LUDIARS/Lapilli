#!/usr/bin/env node
import { spawnOneShot } from './index.js';

// Cross-language entry point. Streaming bytes are inherited, not parsed or re-encoded.
const [file, ...args] = process.argv.slice(2);
if (!file) throw new Error('Usage: ludiars-one-shot <claude|codex> <arguments...>');
const child = spawnOneShot(file, args, { stdio: 'inherit' });
const stop = () => { child.kill('SIGTERM'); };
process.on('SIGINT', stop);
process.on('SIGTERM', stop);
child.on('error', error => { process.stderr.write(`one-shot: ${error.message}\n`); process.exitCode = 1; });
child.on('close', (code) => {
  process.removeListener('SIGINT', stop);
  process.removeListener('SIGTERM', stop);
  process.exitCode = code ?? 1;
});
