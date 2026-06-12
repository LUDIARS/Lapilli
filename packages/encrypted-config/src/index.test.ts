import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { encryptJson, decryptJson, isEncryptedBlob, type EncryptedBlob } from './crypto.js';
import {
  resolveConfigPath,
  resolveMasterSecret,
  readConfigFile,
  writeConfigFile,
  readConfig,
  setConfig,
  deleteConfig,
  type StoreOptions,
} from './store.js';

const MASTER = 'test-master-secret';

// ── crypto ────────────────────────────────────────────────────────────────────

describe('encryptJson / decryptJson', () => {
  it('round-trips a string', () => {
    const blob = encryptJson('hello', MASTER);
    expect(decryptJson<string>(blob, MASTER)).toBe('hello');
  });

  it('round-trips an object', () => {
    const val = { a: 1, b: [true, null] };
    const blob = encryptJson(val, MASTER);
    expect(decryptJson(blob, MASTER)).toEqual(val);
  });

  it('produces unique blobs on each call', () => {
    const b1 = encryptJson('x', MASTER);
    const b2 = encryptJson('x', MASTER);
    expect(b1.salt).not.toBe(b2.salt);
    expect(b1.iv).not.toBe(b2.iv);
  });

  it('throws on wrong master key', () => {
    const blob = encryptJson('secret', MASTER);
    expect(() => decryptJson(blob, 'wrong-key')).toThrow();
  });

  it('throws on tampered data', () => {
    const blob = encryptJson('secret', MASTER);
    const tampered: EncryptedBlob = { ...blob, data: blob.data.slice(0, -4) + 'AAAA' };
    expect(() => decryptJson(tampered, MASTER)).toThrow();
  });
});

describe('isEncryptedBlob', () => {
  it('accepts a valid blob', () => {
    expect(isEncryptedBlob(encryptJson('x', MASTER))).toBe(true);
  });

  it.each([null, undefined, 42, 'str', {}, { v: 2, salt: 'a', data: 'b' }])(
    'rejects %s',
    (x) => expect(isEncryptedBlob(x)).toBe(false),
  );
});

// ── store ─────────────────────────────────────────────────────────────────────

function makeOpts(dir: string, secretKeys: string[] = ['SECRET']): StoreOptions {
  return {
    secretKeys: new Set(secretKeys),
    configPathEnv: 'TEST_CONFIG_PATH',
    masterKeyEnv: 'TEST_MASTER_KEY',
    defaultConfigFile: 'test.config.json',
    masterSecretPrefix: 'test',
  };
}

function makeEnv(dir: string): NodeJS.ProcessEnv {
  return {
    TEST_CONFIG_PATH: join(dir, 'test.config.json'),
    TEST_MASTER_KEY: MASTER,
  };
}

let tmpDir: string;

beforeEach(() => {
  tmpDir = mkdtempSync(join(tmpdir(), 'lapilli-test-'));
});

afterEach(() => {
  rmSync(tmpDir, { recursive: true, force: true });
});

describe('resolveConfigPath', () => {
  it('returns env override when set', () => {
    const opts = makeOpts(tmpDir);
    expect(resolveConfigPath(opts, { TEST_CONFIG_PATH: '/custom/path.json' })).toBe('/custom/path.json');
  });

  it('falls back to cwd/defaultConfigFile', () => {
    const opts = makeOpts(tmpDir);
    const result = resolveConfigPath(opts, {});
    expect(result).toMatch(/test\.config\.json$/);
  });
});

describe('resolveMasterSecret', () => {
  it('returns env override when set', () => {
    const opts = makeOpts(tmpDir);
    expect(resolveMasterSecret(opts, { TEST_MASTER_KEY: 'custom-secret' })).toBe('custom-secret');
  });

  it('falls back to prefix:hostname:user', () => {
    const opts = makeOpts(tmpDir);
    const result = resolveMasterSecret(opts, {});
    expect(result).toMatch(/^test:/);
  });
});

describe('readConfig', () => {
  it('returns null when file does not exist', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    env['TEST_CONFIG_PATH'] = join(tmpDir, 'nonexistent.json');
    expect(readConfig(opts, env)).toBeNull();
  });
});

describe('setConfig / readConfig', () => {
  it('writes plain keys as plaintext', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('PLAIN', 'value', opts, env);
    const cfg = readConfigFile(opts, env);
    expect(cfg.plain['PLAIN']).toBe('value');
    expect(cfg.secrets['PLAIN']).toBeUndefined();
  });

  it('encrypts secret keys', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('SECRET', 'mysecret', opts, env);
    const cfg = readConfigFile(opts, env);
    expect(cfg.secrets['SECRET']).toBeDefined();
    expect(isEncryptedBlob(cfg.secrets['SECRET'])).toBe(true);
    expect(cfg.plain['SECRET']).toBeUndefined();
  });

  it('resolves both plain and secret via readConfig', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('PLAIN', 'pval', opts, env);
    setConfig('SECRET', 'sval', opts, env);
    const result = readConfig(opts, env);
    expect(result?.['PLAIN']).toBe('pval');
    expect(result?.['SECRET']).toBe('sval');
  });

  it('overwrites existing plain key', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('PLAIN', 'first', opts, env);
    setConfig('PLAIN', 'second', opts, env);
    expect(readConfig(opts, env)?.['PLAIN']).toBe('second');
  });

  it('moves key from plain to secrets when reclassified', () => {
    const optsPlain = makeOpts(tmpDir, []);
    const optsSecret = makeOpts(tmpDir, ['KEY']);
    const env = makeEnv(tmpDir);
    setConfig('KEY', 'val', optsPlain, env);
    expect(readConfigFile(optsPlain, env).plain['KEY']).toBe('val');
    setConfig('KEY', 'val', optsSecret, env);
    const cfg = readConfigFile(optsSecret, env);
    expect(cfg.plain['KEY']).toBeUndefined();
    expect(isEncryptedBlob(cfg.secrets['KEY'])).toBe(true);
  });
});

describe('deleteConfig', () => {
  it('removes a plain key', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('PLAIN', 'v', opts, env);
    deleteConfig('PLAIN', opts, env);
    expect(readConfig(opts, env)?.['PLAIN']).toBeUndefined();
  });

  it('removes a secret key', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('SECRET', 'v', opts, env);
    deleteConfig('SECRET', opts, env);
    expect(readConfig(opts, env)?.['SECRET']).toBeUndefined();
  });

  it('does not affect other keys', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    setConfig('PLAIN', 'keep', opts, env);
    setConfig('SECRET', 'keep-secret', opts, env);
    deleteConfig('PLAIN', opts, env);
    const result = readConfig(opts, env);
    expect(result?.['SECRET']).toBe('keep-secret');
    expect(result?.['PLAIN']).toBeUndefined();
  });
});

describe('writeConfigFile / readConfigFile', () => {
  it('preserves structure through write/read cycle', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    const blob = encryptJson('val', MASTER);
    const original = { plain: { A: '1' }, secrets: { B: blob } };
    writeConfigFile(original, opts, env);
    const read = readConfigFile(opts, env);
    expect(read.plain).toEqual({ A: '1' });
    expect(read.secrets['B']).toEqual(blob);
  });

  it('returns empty config on corrupt file', () => {
    const opts = makeOpts(tmpDir);
    const env = makeEnv(tmpDir);
    const path = join(tmpDir, 'test.config.json');
    writeFileSync(path, 'not json');
    // readConfigFile after corrupt write
    const cfg = readConfigFile(opts, { ...env, TEST_CONFIG_PATH: path });
    expect(cfg).toEqual({ plain: {}, secrets: {} });
  });
});
