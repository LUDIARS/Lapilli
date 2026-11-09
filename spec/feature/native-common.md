# feature: native-common — C++ 移植サービス共通の基盤ライブラリ

Node.js から C++ (`native/`) へ移植した 6 サービス (Ingenium / Odeum / Curare / Discutere /
Manus / Tirocinium) が各リポで重複して持っていた基盤コードを 1 か所にまとめたもの。
正本ソース: `native/` (namespace `lapilli`)。TS パッケージ群 (pnpm workspace `packages/*`) とは独立で、
pnpm からは見えない。

---

## 目的

- 同じ不具合を 6 回直さないこと。調査で見つかった重複実装の不具合をここで一度に直した:
  - タイムアウトで孫プロセスが残る → POSIX はプロセスグループ、Windows は Job Object ごと kill。
  - 子の stdin への書き込みで SIGPIPE により親が死ぬ → 書き込みスレッドだけで SIGPIPE を block し、
    発生した保留シグナルは消費する (プロセス全体の signal disposition は変えない)。
  - 子プロセス出力が上限なしで溜まる → Lorica 予算に先に課金してから保持。
  - Windows で TLS の検証用ルート証明書が無い → `ROOT` システムストアを OpenSSL に読み込む
    (既定の verify paths に加えて)。
  - JS と数値の文字列化が合わない / キー順が崩れる → `Number#toString` 互換の出力と
    `nlohmann::ordered_json`。
- 各サービスは自分のエラーメッセージ契約 (Node 版と同じ文言) を保つため、薄いラッパーだけを残す。
  ライブラリはエラーを「種類 (kind) + 既定メッセージ」で返し、文言はラッパーが決められる。

## 依存 (閉じた許可リスト)

C++20 標準ライブラリ / OS API / Boost 1.87.0 のヘッダ (Asio・Beast、`boost-1.87.0-b2-nodocs.zip`
SHA256 `40efbb1f…0d42`、ヘッダのみ = SOURCE_SUBDIR に存在しないディレクトリを指定) /
nlohmann_json 3.11.3 (`json.tar.xz` SHA256 `d6c65aca…757d`) / OpenSSL 3 (`find_package`) /
Lorica commit `cab4f12086e30484f249d1b4aa479bf7cdec13a9` (`Lorica::boundary`, `lorica_harden_target`)。
これ以外は追加しない。

## 不変条件

- すべての外部入力 (JSON テキスト・値・深さ、行、HTTP ヘッダ・ボディ、子プロセス出力、ファイル、
  ディレクトリ列挙) は保持する前に Lorica 予算へ課金する。上限ちょうどは受理、+1 は拒否。
- 子プロセスはシェルを通さない (argv のみ)。Windows では `.bat` / `.cmd` を起動しない
  (CreateProcess が cmd.exe に渡すため)。
- 暗号・乱数は OpenSSL のみ (`RAND_bytes`, `EVP_*`)。自前実装・`std::random_device` 不可。
- 秘密値・ゲーム名をコード / テスト / ログに書かない。HTTP クライアントのエラーメッセージに
  ヘッダ値・ボディ・URL の資格情報を含めない。
- すべてのライブラリ / テストターゲットに `lorica_harden_target()`。GCC `-Wall -Wextra -Wpedantic`
  で警告ゼロ、MSVC `/W4 /permissive-` でも警告が出ない書き方 (暗黙の縮小変換なし、GCC 拡張なし)。
  Windows 経路 (Win32) も実装する (このリポの CI でビルドできるのは Linux のみ)。

## パート別 API

### `Lapilli::core`

| ヘッダ | 名前空間 | 内容 |
|---|---|---|
| `lapilli/budget.hpp` | `lapilli` | `BudgetExceeded`, `Budget` (Lorica 予算の throw 版), `byte_limits`, `unlimited`, `checked_add/multiply` |
| `lapilli/json.hpp` | `lapilli::json` | `Json = nlohmann::ordered_json`、`JsonLimits{max_bytes,max_elements,max_depth}`、`parse(text, limits)` → `JsonError{too_large\|too_deep\|invalid}`、`parse(text, lorica::input_budget&)` (既存予算に課金)、`try_parse(…, &why, &kind)`、`stringify(v, indent)` (= `JSON.stringify`)、`number_to_js_string` (= `String(n)`: `NaN`/`Infinity`)、`number_to_json` (非有限は `null`)、`append_json_string/number`、`js_number_value` (2^53 超の整数は JSON.parse と同じく double に丸める) |
| `lapilli/text.hpp` | `lapilli::text` | `is_valid_utf8` / `sanitize_utf8` (WHATWG デコーダと同じ: 最大不正部分ごとに U+FFFD 1 個)、`to_utf16/to_utf8`、`js_length` / `js_compare` / `JsLess` / `js_slice` (UTF-16 単位)、`js_trim(_start/_end)`、`js_split` / `js_join`、`ascii_lower/upper/iequals`、`split(text, char)`、`starts_with` / `ends_with` |
| `lapilli/js_math.hpp` | `lapilli::js_math` | `js_min/max` (NaN 伝播・-0 < +0)、`js_round`、`js_to_number` (= `Number(text)`)、`js_to_fixed`、`js_is_integer`、`js_is_safe_integer` |
| `lapilli/process.hpp` | `lapilli::process` | 同期 `run(Spec, const std::atomic<bool>* cancel)` → `Result{spawn_error?{kind,errno_name,message}, exit_code, signal, timed_out, cancelled, output_overflow, out, err, err_dropped, duration}`。`Spec{argv, cwd, env_mode (inherit_overlay/replace/empty), env, env_remove, stdin_data, timeout, max_stdout, max_stderr, capture_stdout, stderr_tail_only, hide_window}`。`find_executable`, `find_executable_with(SearchRules)` (Windows の PATHEXT 規則を任意 OS でテスト可能)、`quote_windows_argument`, `windows_command_line`, `is_shell_script_name`, `current_environment`, `env_get` |
| `lapilli/lines.hpp` | `lapilli::lines` | `LineSplitter{max_line, overflow=throw_error\|truncate_emit\|discard, terminators=lf_crlf\|lf_crlf_cr, sanitize_utf8}` の `push` / `finish` / `take_completed`、`LineReader` (istream、届いた分だけ読む)、`split_lines` |
| `lapilli/fs.hpp` | `lapilli::fs` | `read_file(path, max)` → `{ok\|missing\|too_large\|io_error}`、`read_file_or_throw`、`write_file` / `append_file`、`write_file_atomic` (同ディレクトリに排他作成 → fsync → rename)、`list_files_recursive` (シンボリックリンクを辿らない・列挙予算・JS 順ソート)、`to_posix` / `to_utf8` / `from_utf8` |
| `lapilli/cli.hpp` | `lapilli::cli` | `node:util` `parseArgs` (strict) の移植。エラー文言は Node と同一 |
| `lapilli/ordered.hpp` | `lapilli` | `OrderedMap` / `OrderedSet` (JS の Map / Set の挿入順) |
| `lapilli/url.hpp` | `lapilli::url` | WHATWG URL の部分集合: `parse`, `resolve` (相対参照すべて)、`Url::origin/href/bare_hostname/effective_port`、`percent_decode`, `encode_uri_component`, `decode_uri_component`, `parse_query` / `serialize_query` / `query_get(_all)` (URLSearchParams)、`parse_ipv4` (厳格)、`parse_ipv4_host` / `parse_ipv6_host`。IDNA は非対応 (拒否) |
| `lapilli/test/check.hpp` | `lapilli::test` | 登録式の最小テストハーネス (`LAPILLI_TEST`, `LAPILLI_CHECK`, `LAPILLI_CHECK_EQ` は値をコピーして比較, `LAPILLI_CHECK_THROWS(_AS)`)。`Lapilli::test_main` が `main` (第 1 引数で名前フィルタ) |

### `Lapilli::crypto` (`lapilli/crypto.hpp`, `lapilli::crypto`)

`sha256_raw/hex`, `hmac_sha256(_hex)` (EVP_Q_mac), `random_bytes/random_hex` (RAND_bytes、失敗時は例外で
代替源なし), `constant_time_equal` (CRYPTO_memcmp), `cleanse`, `to_hex/from_hex`,
`base64_encode/decode` (厳格・正規パディング), `base64url_encode/decode` (パディングなし・厳格)。

### `Lapilli::one_shot` (`lapilli/one_shot.hpp`, `lapilli::one_shot`)

`packages/one-shot` (TS) の C++ 版。`subscription_environment` (API キー・クラウド選択子・
`CLAUDECODE`・`CONCORDIA_HOOK`・`LICTOR_*` を除去)、`resolve_model` / `resolve_model_args`
(役割名スナップショット 2026-10-03、`LUDIARS_ONESHOT_MODEL_<ROLE>` 上書き、`[1m]` 等の文脈接尾辞を保持、
Codex の CLI 固有 ID を許可)、`detect_provider`、`resolve_executable(name, lookup, Policy)`:

- `native_only`: ネイティブ実行ファイルのみ。Windows は PATH 順 → 各ディレクトリ内で (1) `.exe`/`.com`
  (PATHEXT 順) (2) npm パッケージ同梱の native バイナリ (3) npm `.cmd` シムの `%dp0%` ターゲットが
  `.exe` ならそれ。シムがスクリプト (Node 版 CLI) を指すなら拒否。POSIX は ELF / Mach-O のみ、`#!` は拒否。
- `allow_node_entry`: TS と同じ。Windows では npm の JS 入口を `[node, entry]` で起動、POSIX では渡された
  コマンドをそのまま起動。
- どちらの方針でも `.cmd` / `.bat` は起動しない。失敗は `ResolutionError{kind, path}`
  (`not_found`, `not_native`, `script`, `shim_without_target`, `shim_target_missing`, `unspawnable`, `node_missing`)。

`prepare(file, args, PrepareOptions{policy, env, lookup})` は `cmd /c <cli>` の包みを (シェル文字列を
実行せずに) 解き、プロバイダ判定・環境・Git Bash パス (Windows)・モデル引数を揃えた `Launch` を返す。
`run(launch, prompt, RunOptions, cancel)` は `lapilli::process` で 1 回だけ起動する (prompt は stdin、
期限・出力上限・stderr は末尾のみ・キャンセル、タイムアウト時はプロセスツリーごと kill、再試行なし)。

### `Lapilli::net` (`lapilli/http/*.hpp`, `lapilli::http`)

- `Server` (Beast): `listen()` が実際の待受ポートを返す (既定 127.0.0.1)。`run()` / `start()` /
  `stop()` / `wait()` / `stop_on_signals()` (SIGINT/SIGTERM)。ハンドラは `(ServerRequest&&, Responder)` で
  非同期に応答可能 (`Responder::send` はどのスレッドからでも 1 回、以後 false)。`synchronous()` で同期関数を包む。
  `ServerLimits`: `header_bytes` (超過 431), `body_bytes` (超過 413、巨大 Content-Length も即 413),
  `max_connections` (超過分は読まずに閉じる), `read_timeout`, `write_timeout`, `handler_timeout`
  (超過 503), `requests_per_connection`。壊れた要求 400、ハンドラ例外 / 応答せずに Responder 破棄は 500。
  HEAD は Content-Length を保ってボディなし。
- `resolve_port(env_value, default, var_name)`: Ingenium の文言 `"<VAR> must be an integer between 1 and 65535 (got '<value>')"` を変数名だけ差し替えて再現。
- `fetch(ClientRequest)` (同期) / `try_fetch`: `FetchError{kind}` (`invalid_url`, `unsupported_scheme`,
  `credentials`, `blocked_address`, `dns`, `connect`, `tls`, `timeout`, `cancelled`, `too_large`,
  `header_too_large`, `protocol`, `redirect`)。redirect `manual | error | follow(max_redirects)`
  (301/302 の POST と 303 は GET 化・ボディ系ヘッダ除去、オリジンが変わると Authorization/Cookie 除去)。
  `AddressPolicy::public_only` は解決した全アドレスを検査し、検査済みアドレスにだけ接続する
  (DNS rebinding の TOCTOU なし、ホップごとに再検査)。`pinned_addresses`。connect / 全体の期限、
  キャンセル、`header_limit`、`max_body` (Lorica)、multipart (`FormPart`, 乱数境界)、
  ストリーミング (`on_head` / `on_chunk`)。
- `configure_client_tls(SSL_CTX*)`: TLS 1.2 以上・ピア検証・既定 verify paths + Windows ROOT ストア。
  独自の `asio::ssl::context` を持つサービスは `context.native_handle()` を渡して共通化できる。
  `set_server_name` は IP リテラルには SNI を送らない。
- `is_public_address`: SSRF 判定表 (RFC1918, CGNAT, loopback, link-local, 文書用, ベンチマーク,
  マルチキャスト, 予約, ULA, site-local, discard, IPv4-mapped / NAT64 / 6to4 内の IPv4 も判定)。

## 既定の上限

| 対象 | 既定値 |
|---|---|
| JSON | 16 MiB / 2Mi 要素 / 深さ 256 |
| 行 | 16 MiB / 行 |
| 子プロセス出力 | stdout・stderr 各 32 MiB (one_shot は stdout 8 MiB・stderr 末尾 1 MiB)、期限 2 分 (one_shot 5 分) |
| ディレクトリ列挙 | 200000 エントリ |
| HTTP サーバ | ヘッダ 8 KiB / ボディ 1 MiB / 64 接続 / 読み 10 s / 書き 10 s / 100 要求/接続 |
| HTTP クライアント | 接続 15 s / 全体 30 s / ヘッダ 64 KiB / ボディ 16 MiB / リダイレクト 5 |

ファイル読みは呼び出し側が上限を必ず渡す (既定値なし)。

## 取り込み方 (consumer)

```cmake
include(FetchContent)
set(LAPILLI_SOURCE_DIR "" CACHE PATH "Local Lapilli checkout (empty = fetch the pinned commit)")
if(LAPILLI_SOURCE_DIR)
  add_subdirectory("${LAPILLI_SOURCE_DIR}/native" "${CMAKE_BINARY_DIR}/_deps/lapilli-build")
else()
  FetchContent_Declare(lapilli GIT_REPOSITORY https://github.com/LUDIARS/Lapilli.git
    GIT_TAG <固定コミット SHA> SOURCE_SUBDIR native)
  FetchContent_MakeAvailable(lapilli)
endif()
target_link_libraries(my_target PRIVATE Lapilli::core Lapilli::net)
```

- 依存の再利用: consumer が既に `Lorica::boundary` / `nlohmann_json::nlohmann_json` を持っていればそれを使う
  (`if(NOT TARGET …)`)。Boost は `LAPILLI_BOOST_TARGET=<既存の INTERFACE ターゲット>` で consumer のものを使う。
  FetchContent の宣言名は `lorica` / `json` / `boost` なので、consumer が同名で先に宣言していればそちらが優先され、
  `FETCHCONTENT_SOURCE_DIR_LORICA/JSON/BOOST` や `LORICA_SOURCE_DIR` でローカルコピーも指せる。
- オプション: `LAPILLI_NATIVE_CRYPTO` / `LAPILLI_NATIVE_ONE_SHOT` / `LAPILLI_NATIVE_NET` (既定 ON。NET は CRYPTO が必要)、
  `LAPILLI_NATIVE_TESTS` (トップレベル時のみ既定 ON)。
- `GIT_TAG` は必ずコミット SHA で固定する (ブランチ名・タグ名不可)。

## 各リポに残す薄いラッパー

ライブラリは種類 (kind) と既定文言を返す。Node 版と同じ文言を契約として持つリポは、次のような
薄いラッパーを自リポに残す。

| リポ | 残すもの |
|---|---|
| Ingenium | `ig::Json` 等の別名、`ig::BudgetExceeded` / `JsonParseError` への例外変換 (`what: …` 接頭辞)、`limits::*` 定数、`resolve_port` (`INGENIUM_PORT`, 4430)、`LineChannel` (ProtocolError 変換)、`HttpError` 変換、`resolve_cli_command` の文言 (`install Claude Code …`)、`claude_args` / ClaudeCliLlm |
| Odeum | Asio 非同期のプロセス・HTTP (当面は自前; 後述)、Odeum 固有のエラー JSON |
| Curare | `TransportError` 文字列 kind への変換、WebSocket / IPC / Gate、Ed25519 |
| Discutere | `di::proc::Options` (`should_cancel` 関数) → `atomic<bool>` 変換、設定・ログの文言 |
| Manus | `bounded_json` の呼び出し規約、静的ファイル配信、WebSocket セッション |
| Tirocinium | `tr::app::prepare_one_shot` (EnvList 形のまま) の転送、`is_blocked_ip` → `is_public_address`、PASETO |

## テスト

`cmake -S native -B <build> && cmake --build <build> -j2 && ctest` (ローカル依存は
`-DLORICA_SOURCE_DIR=… -DFETCHCONTENT_SOURCE_DIR_BOOST=… -DFETCHCONTENT_SOURCE_DIR_JSON=…`)。
14 実行ファイル。JS 互換関数は Node v22 で記録した値と比較。境界テスト (ちょうど / +1 / 壊れた入力 /
巨大入力) を JSON・行・子プロセス出力・HTTP サーバ / クライアントに持つ。HTTP は 127.0.0.1 の
ローカル Beast サーバだけを使い、外部サービスには接続しない。

## 未対応 (follow-up)

- Asio 非同期版の `process` / `fetch` (Odeum の ChildWatcher・ストリーミング用途)。同期版を
  専用スレッドで使うか、Odeum の実装を移す。
- WebSocket (Curare / Manus / Tirocinium / Discutere がそれぞれ持つ) の共通化。
- `JSON.parse("1e400")` は JS では Infinity だが本ライブラリでは `invalid`。
- IDNA (非 ASCII ホスト名) は拒否。
