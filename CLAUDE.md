# Lapilli — Claude 向けメモ

LUDIARS 小型共有ツール集。略称 **La**。

## これは何か

複数サービスで使う小さなユーティリティライブラリを束ねる pnpm monorepo。
各パッケージは `@ludiars/` スコープで GitHub Packages に publish する。

Node サービスの C++17 移植で共有する **C++ パーツ** も `cpp/` に置く (pnpm とは独立した CMake
プロジェクト)。決めごとと使い方は `cpp/README.md`。

## パッケージ一覧

| パッケージ | 説明 |
|---|---|
| `@ludiars/encrypted-config` | AES-256-GCM + scrypt によるローカル暗号化 config |
| `@ludiars/mail-inbox` | Gmail API v1 の読み取り、MIME 本文・添付メタデータ抽出 |
| `@ludiars/llm-gateway` | LLM トークン節約の request 整形 (prefix-cache 順序付け / ローリング要約 / tier ルーティング)。transport 非依存 |
| `@ludiars/blackbox` | 成長型ブラックボックス (LLM 判断→ルール昇格→LLM 卒業)。影評価で人間クリック無しでも学習が前進。SQLite/インメモリ両対応 |
| `@ludiars/log-weaver` | 安定稼働 probe 群 (安全網 / event-loop lag / async ガード / 子プロセス監視 / aspect)。Augur の log injection が注入するランタイム。Vg 互換イベント、transport 非依存 |
| `native/` (C++) | C++ 移植サービス共通の基盤 (namespace `lapilli`、CMake ターゲット `Lapilli::core` / `crypto` / `one_shot` / `net` / `test_main`)。JS 互換 JSON・UTF-16 文字列・子プロセス (プロセスツリー kill)・行分割・境界付きファイル I/O・OpenSSL 暗号・parseArgs・WHATWG URL・Beast HTTP サーバ/クライアント (SSRF 方針)・one-shot 起動境界。pnpm workspace 外。仕様 `spec/feature/native-common.md` |

## C++ パーツ一覧

| パーツ | ターゲット | 説明 |
|---|---|---|
| `cpp/sqlite` | `lapilli::sqlite` | システムの SQLite3 の薄いラッパー (prepared statement + bind / Transaction / Error)。WAL・busy timeout 対応、1 接続 1 スレッド |

- 取得・同梱したサードパーティのコードを置かない。依存はシステムライブラリだけ (`find_package`)。
- 書き方は Portus の C++ 版に揃える (タブ、日本語の `//` コメント、1 ファイル 1 責務、自前の最小テスト枠)。

```sh
cmake -S cpp -B cpp/build -G Ninja && cmake --build cpp/build && ctest --test-dir cpp/build --output-on-failure
# 変更したら ASan + UBSan と TSan でも (別のビルドディレクトリで)
cmake -S cpp -B cpp/build-asan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S cpp -B cpp/build-tsan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=thread"
# 任意: Lorica のハードニング (CMake 3.24+)
cmake -S cpp -B cpp/build -G Ninja -DLAPILLI_LORICA_DIR=/path/to/lorica
```

## branch 運用

- substantive な編集は feat/ ブランチ + PR (main 直編集しない)
- Ars 配下なので CI green ならオートマージ (squash+delete) 可

## publish

- `v*` タグ push → `.github/workflows/publish.yml` が build + `npm publish`
- GitHub Packages の可視性は publish 後に **Internal** に変更する (UI 手動操作)
- consumer 側は `.npmrc` に `@ludiars:registry=https://npm.pkg.github.com` を追加

## 関連

- Vestigium (Vg) — JSONL ロガー、将来的にここへ移行予定
