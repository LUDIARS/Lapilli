# Lapilli — Claude 向けメモ

LUDIARS 小型共有ツール集。略称 **La**。

## これは何か

複数サービスで使う小さなユーティリティライブラリを束ねる pnpm monorepo。
各パッケージは `@ludiars/` スコープで GitHub Packages に publish する。

## パッケージ一覧

| パッケージ | 説明 |
|---|---|
| `@ludiars/encrypted-config` | AES-256-GCM + scrypt によるローカル暗号化 config |
| `@ludiars/mail-inbox` | Gmail API v1 の読み取り、MIME 本文・添付メタデータ抽出 |
| `@ludiars/llm-gateway` | LLM トークン節約の request 整形 (prefix-cache 順序付け / ローリング要約 / tier ルーティング)。transport 非依存 |
| `@ludiars/blackbox` | 成長型ブラックボックス (LLM 判断→ルール昇格→LLM 卒業)。影評価で人間クリック無しでも学習が前進。SQLite/インメモリ両対応 |
| `@ludiars/log-weaver` | 安定稼働 probe 群 (安全網 / event-loop lag / async ガード / 子プロセス監視 / aspect)。Augur の log injection が注入するランタイム。Vg 互換イベント、transport 非依存 |
| `native/` (C++) | C++ 移植サービス共通の基盤 (namespace `lapilli`、CMake ターゲット `Lapilli::core` / `crypto` / `one_shot` / `net` / `test_main`)。JS 互換 JSON・UTF-16 文字列・子プロセス (プロセスツリー kill)・行分割・境界付きファイル I/O・OpenSSL 暗号・parseArgs・WHATWG URL・Beast HTTP サーバ/クライアント (SSRF 方針)・one-shot 起動境界。pnpm workspace 外。仕様 `spec/feature/native-common.md` |

## branch 運用

- substantive な編集は feat/ ブランチ + PR (main 直編集しない)
- Ars 配下なので CI green ならオートマージ (squash+delete) 可

## publish

- `v*` タグ push → `.github/workflows/publish.yml` が build + `npm publish`
- GitHub Packages の可視性は publish 後に **Internal** に変更する (UI 手動操作)
- consumer 側は `.npmrc` に `@ludiars:registry=https://npm.pkg.github.com` を追加

## 関連

- Vestigium (Vg) — JSONL ロガー、将来的にここへ移行予定
