# feature: cpp/sqlite — C++ の SQLite ラッパー (`lapilli::sqlite`)

Lapilli 最初の C++ パーツ。Node サービスを C++17 へ移すときに、SQLite を使う複数のサービスが
共有する薄いラッパー。システムの SQLite3 にリンクし、取得・同梱したコードは持たない。

正本ソース: `cpp/sqlite/include/lapilli/sqlite.hpp`, `cpp/sqlite/src/*.cpp`
使い方: `cpp/sqlite/README.md` / C++ 領域全体: `cpp/README.md`

---

## 目的 / ユーザーストーリー

- 最初の利用者は Memoria の `server/owntracks-server.ts` の C++ 移植。Memoria 本体 (Node +
  better-sqlite3) が使っている WAL の SQLite DB を同時に開き、`insertGpsLocation`
  (`server/db.ts`) の SELECT (重複確認) / SELECT (直近 2 行) / UPDATE (停止区間の圧縮) /
  INSERT を実行したい。
- better-sqlite3 の `.prepare().get/.all/.run` と `lastInsertRowid` に相当する操作を、
  値を必ず bind で渡す形で、C++ から安全に使いたい。
- サービスごとに SQLite の扱い (エラー確認・bind の寿命・ロック待ち) を書き直したくない。

## 振る舞い (入力 → 処理 → 出力)

- **開く** `Database::open(path, OpenOptions)`
  - `create=false` (既定) で無いファイル → `SQLITE_CANTOPEN` の `Error`。ファイルは作らない。
  - `read_only` → 書き込みは `SQLITE_READONLY` の `Error`。`read_only` と `create` の同時指定は
    `SQLITE_MISUSE`。
  - 拡張結果コードを有効にし、`busy_timeout_ms` (既定 5000) を設定し、`foreign_keys` (既定 ON)
    を PRAGMA で設定する。`wal=true` なら `journal_mode=WAL` を設定し、結果が `wal` でなければ
    `Error` (黙って別のモードで続けない)。
- **実行** `exec(sql)` は結果を返さない文 (複数可) をそのまま実行する。`prepare(sql)` は文を
  1 つだけ受け、2 つ目の文・空の SQL は `Error`。
- **bind** (1 始まり) — int64 / double / UTF-8 text / blob / NULL / `std::optional<T>`
  (nullopt → NULL)。text と blob は `SQLITE_TRANSIENT` で複製する。空の text は `''`、
  長さ 0 の blob は `X''` (NULL にならない)。`bool` と int64 に収まらない unsigned は受けない。
  `bind_all(args...)` は数が文のパラメータ数と違えば `SQLITE_RANGE`。
- **取り出し** `step()` → 行があれば true。列 (0 始まり) は行の上でだけ読め、範囲外は
  `SQLITE_RANGE`、行が無いときは `SQLITE_MISUSE`。text と blob は途中の NUL も含めて返す。
- **補助** `run()` → `RunResult{changes, last_insert_rowid}` (行が返る文は `Error`)。
  `first(read)` → `std::optional`。`for_each(fn)`。3 つとも終われば (例外でも) 文を巻き戻す
  (bind は残る)。`reset()` は巻き戻して bind も消す。
- **トランザクション** `Transaction(db, mode)` — 既定 `BEGIN IMMEDIATE`。`commit()` しないで
  破棄されたら ROLLBACK (破棄は例外を投げない)。
- **列一覧** `table_columns(table)` → `PRAGMA table_info` の列名 (定義順)。無いテーブルは空。
  テーブル名は bind できないので、識別子でなければ `SQLITE_MISUSE`。
- **失敗** すべての `sqlite3_*` の戻り値を確かめ、`Error` (一次コード `code()` と拡張コード
  `extended_code()`、SQLite のメッセージ付き) を投げる。`step` の失敗は文を巻き戻してから投げる
  ので、制約違反の後も DB と文は使える。

## 制約 / 既知の制限

- 1 つの `Database` は 1 つのスレッドで使う。ラッパーはロックを持たない。スレッドごとに開く。
- ロック待ちは `busy_timeout_ms` まで。過ぎたら `SQLITE_BUSY` の `Error`。再試行は呼び出し側。
- `Statement` を `Database` より長く生かさない。生かした場合は使うと `SQLITE_MISUSE`
  (`sqlite3_close_v2` と弱参照で検出。破棄は安全)。
- 値を SQL 文字列へ埋め込む補助は持たない。SQL は呼び出し側が渡したまま使う。
- 列の読み出しは SQLite の型変換に従う (NULL は 0 / 0.0 / 空)。NULL を区別するなら
  `column_is_null` / `column_optional<T>`。
- path は UTF-8 のファイル名として渡す (URI としては解釈しない)。
- Windows はコード上 POSIX 依存を持たないが、SQLite 付きでのビルド・テストは未検証。

## テスト

`cpp/sqlite/tests/` (CTest の `lapilli_sqlite_tests`)。一時ディレクトリの一意な名前のファイルを
使い、終われば消す。外部サービスに繋がない。gps_locations の DDL と `insertGpsLocation` の
3 分岐 (重複 → skip / 停止中の 3 点 → UPDATE / 移動 → INSERT) をテスト内に写して、API が足りる
ことを確かめる。ASan + UBSan と TSan でも通す。

## 関連

- C++ 領域の決めごと: [cpp/README.md](../../cpp/README.md)
- ビルド / テスト: [setup/build-test.md](../setup/build-test.md) (TypeScript 側)
