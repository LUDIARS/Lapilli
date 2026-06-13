# Lapilli — Claude 向けメモ

LUDIARS 小型共有ツール集。略称 **La**。

## これは何か

複数サービスで使う小さなユーティリティライブラリを束ねる pnpm monorepo。
各パッケージは `@ludiars/` スコープで GitHub Packages に publish する。

## パッケージ一覧

| パッケージ | 説明 |
|---|---|
| `@ludiars/encrypted-config` | AES-256-GCM + scrypt によるローカル暗号化 config |
| `@ludiars/llm-gateway` | LLM トークン節約の request 整形 (prefix-cache 順序付け / ローリング要約 / tier ルーティング)。transport 非依存 |

## branch 運用

- substantive な編集は feat/ ブランチ + PR (main 直編集しない)
- Ars 配下なので CI green ならオートマージ (squash+delete) 可

## publish

- `v*` タグ push → `.github/workflows/publish.yml` が build + `npm publish`
- GitHub Packages の可視性は publish 後に **Internal** に変更する (UI 手動操作)
- consumer 側は `.npmrc` に `@ludiars:registry=https://npm.pkg.github.com` を追加

## 関連

- Vestigium (Vg) — JSONL ロガー、将来的にここへ移行予定
