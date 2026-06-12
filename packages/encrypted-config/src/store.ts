// ローカル暗号化 config ストア (Canalis 方式)。
//
// 保存先: リポジトリ直下 <service>.config.json (gitignore 済)。
//         env override: <PREFIX>_CONFIG_PATH
//
// フォーマット: { plain: Record<string,string>, secrets: Record<string,EncryptedBlob> }
//   - 非シークレット (port / host / backend 等) は plain に平文保存。
//   - シークレット (API キー / Bot トークン) は AES-256-GCM EncryptedBlob として保存。
//
// master secret: env <PREFIX>_MASTER_KEY → マシン束縛値 (<service>:hostname:user)。

import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { hostname, userInfo } from 'node:os';
import { encryptJson, decryptJson, isEncryptedBlob, type EncryptedBlob } from './crypto.js';

export type { EncryptedBlob };

/** config ファイルのディスク上フォーマット。 */
export interface ConfigFile {
  /** 非シークレット: 平文文字列マップ。 */
  plain: Record<string, string>;
  /** シークレット: AES-256-GCM EncryptedBlob マップ。 */
  secrets: Record<string, EncryptedBlob>;
}

/** 全 config を平文 map として返す型。 */
export type ResolvedConfig = Record<string, string>;

/** ストアのオプション。 */
export interface StoreOptions {
  /** 暗号化保存するキーの Set。それ以外は plain に保存。 */
  secretKeys: Set<string>;
  /** config ファイルパスの env 変数名 (例: 'TIROCINIUM_CONFIG_PATH')。 */
  configPathEnv: string;
  /** master secret の env 変数名 (例: 'TIROCINIUM_MASTER_KEY')。 */
  masterKeyEnv: string;
  /** デフォルト config ファイル名 (例: 'tirocinium.config.json')。 */
  defaultConfigFile: string;
  /** master secret のフォールバック prefix (例: 'tirocinium' → 'tirocinium:hostname:user')。 */
  masterSecretPrefix: string;
}

/** config ファイルパスを解決する。 */
export function resolveConfigPath(opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): string {
  const override = env[opts.configPathEnv];
  if (override && override.length > 0) return override;
  return join(process.cwd(), opts.defaultConfigFile);
}

/** master secret を解決する。 env → マシン束縛値。 */
export function resolveMasterSecret(opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): string {
  const override = env[opts.masterKeyEnv];
  if (override && override.length > 0) return override;
  return `${opts.masterSecretPrefix}:${hostname()}:${userInfo().username}`;
}

/** config ファイルを読む。未存在 / 破損なら空 config 扱い。 */
export function readConfigFile(opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): ConfigFile {
  const path = resolveConfigPath(opts, env);
  if (!existsSync(path)) return { plain: {}, secrets: {} };
  try {
    const parsed = JSON.parse(readFileSync(path, 'utf8')) as ConfigFile;
    return {
      plain: parsed.plain && typeof parsed.plain === 'object' ? parsed.plain : {},
      secrets: parsed.secrets && typeof parsed.secrets === 'object' ? parsed.secrets : {},
    };
  } catch {
    return { plain: {}, secrets: {} };
  }
}

/** config ファイルを書く (2-space JSON)。 */
export function writeConfigFile(cfg: ConfigFile, opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): void {
  const path = resolveConfigPath(opts, env);
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, `${JSON.stringify(cfg, null, 2)}\n`, 'utf8');
}

/**
 * 全 config を読んで平文 map を返す (シークレットは復号)。
 * ファイル未存在 → null。復号失敗キーは skip (master 鍵変更時等)。
 */
export function readConfig(opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): ResolvedConfig | null {
  if (!existsSync(resolveConfigPath(opts, env))) return null;
  const cfg = readConfigFile(opts, env);
  const ms = resolveMasterSecret(opts, env);
  const out: ResolvedConfig = { ...cfg.plain };
  for (const [key, blob] of Object.entries(cfg.secrets)) {
    if (!isEncryptedBlob(blob)) continue;
    try {
      out[key] = decryptJson<string>(blob, ms);
    } catch {
      // master 鍵変更 / 改竄時は無視
    }
  }
  return out;
}

/** 1 キーを config ファイルに書く。secretKeys に含まれれば暗号化、それ以外は平文。 */
export function setConfig(key: string, value: string, opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): void {
  const cfg = readConfigFile(opts, env);
  if (opts.secretKeys.has(key)) {
    cfg.secrets[key] = encryptJson(value, resolveMasterSecret(opts, env));
    delete cfg.plain[key];
  } else {
    cfg.plain[key] = value;
    delete cfg.secrets[key];
  }
  writeConfigFile(cfg, opts, env);
}

/** 1 キーを config ファイルから削除。 */
export function deleteConfig(key: string, opts: StoreOptions, env: NodeJS.ProcessEnv = process.env): void {
  const cfg = readConfigFile(opts, env);
  delete cfg.plain[key];
  delete cfg.secrets[key];
  writeConfigFile(cfg, opts, env);
}
