# setup: publish / consumer 利用

各パッケージは `@ludiars/` スコープで GitHub Packages (`npm.pkg.github.com`) に publish する。

正本ソース: `.github/workflows/publish.yml` / 各パッケージ `package.json` の `publishConfig` /
ルート `CLAUDE.md`

---

## publish フロー (`v*` タグ駆動)

`.github/workflows/publish.yml` は `push: tags: ['v*']` で起動する:

1. checkout → pnpm 9 / Node 22 セットアップ (`registry-url: npm.pkg.github.com`, `scope: @ludiars`)。
2. `pnpm install --frozen-lockfile`
3. `pnpm -r run build`
4. **未公開バージョンだけ publish**: `packages/*/` を走査し、
   `npm view <name>@<version>` で既公開なら skip、未公開なら
   `(cd <dir> && pnpm publish --no-git-checks)`。
   - 一括 publish だと既公開版で 409 になりジョブが落ちるための個別判定。
   - 認証は `NODE_AUTH_TOKEN: secrets.GITHUB_TOKEN`。

### リリース手順

1. 対象パッケージの `package.json` の `version` を上げてマージ。
2. `v<version>` タグを push (例 `git tag v0.1.1 && git push origin v0.1.1`)。
3. CI が未公開分のみ publish。

### publish 後の手作業

- GitHub Packages の可視性を **Internal** に変更する (UI 手動操作、`CLAUDE.md` 記載)。

## 各パッケージの publish 設定

`packages/<pkg>/package.json`:
- `publishConfig.registry: https://npm.pkg.github.com`
- `files: ["dist"]` (成果物のみ同梱)
- `main: dist/index.js` / `types: dist/index.d.ts` / `exports["."]` (import + types)

現行バージョン: `@ludiars/encrypted-config` 0.1.0 / `@ludiars/llm-gateway` 0.1.0。

## consumer 側の利用

- consumer リポの `.npmrc` に追記:
  ```
  @ludiars:registry=https://npm.pkg.github.com
  ```
- GitHub Packages は認証必須。consumer 側で GitHub PAT (read:packages) を `.npmrc` の
  `//npm.pkg.github.com/:_authToken=...` か env で渡す。
- インストール後 `import { ... } from '@ludiars/<pkg>'` で利用 (ESM)。
