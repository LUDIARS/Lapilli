# lapilli::sqlite

SQLite の薄いラッパー。システムの SQLite3 (`find_package(SQLite3)`) にリンクする。
公開ヘッダ `lapilli/sqlite.hpp` は `sqlite3.h` を読まない。

仕様: [spec/feature/cpp-sqlite.md](../../spec/feature/cpp-sqlite.md)

## 使い方

```cpp
#include <lapilli/sqlite.hpp>

using namespace lapilli::sqlite;

OpenOptions o;
o.wal = true;	//別プロセス (better-sqlite3 など) と同じ DB を使うとき
o.busy_timeout_ms = 5000;
Database db = Database::open("/path/to/memoria.db", o);	//無いファイルは開かない (create=false)

//値は必ず bind で渡す。SQL に値を埋め込まない
Statement dup = db.prepare("SELECT id FROM gps_locations WHERE user_id = ? AND recorded_at = ? LIMIT 1");
dup.bind_all(user_id, recorded_at);
std::optional<std::int64_t> id = dup.first([](const Statement& r) { return r.column_int64(0); });

Statement ins = db.prepare("INSERT INTO gps_locations (user_id, device_id, lat, lon, accuracy_m) VALUES (?, ?, ?, ?, ?)");
std::optional<std::string> device;	//nullopt は NULL
std::optional<double> accuracy = 8.5;
RunResult r = ins.bind_all(user_id, device, 35.0, 135.0, accuracy).run();
std::int64_t new_id = r.last_insert_rowid;

Statement recent = db.prepare("SELECT id, lat, lon, samples_first_at FROM gps_locations ORDER BY recorded_at DESC LIMIT 2");
recent.for_each([&](const Statement& row) {
	std::optional<std::string> first_at = row.column_optional<std::string>(3);
	//...
});

{
	Transaction tx(db);	//BEGIN IMMEDIATE。commit しないで抜けたら ROLLBACK
	db.exec("UPDATE ...");
	tx.commit();
}
```

## API

- `Database::open(path, OpenOptions)` — `read_only` / `create` (既定 false: 無いファイルは
  `SQLITE_CANTOPEN` の Error) / `busy_timeout_ms` (既定 5000) / `wal` (true なら
  `journal_mode=WAL`、できなければ Error) / `foreign_keys` (既定 true)
- `exec(sql)` / `prepare(sql)` / `last_insert_rowid()` / `changes()` / `table_columns(table)` / `close()`
- `Statement` — `bind(index, v)` (1 始まり。整数・浮動小数・文字列・`BlobView`・
  `std::vector<std::uint8_t>`・`nullptr`・`std::nullopt`・`std::optional<T>`)、
  `bind_int64/double/text/blob/null`、`bind_all(args...)`、`step()`、`reset()`、
  `column_count/column_name/column_is_null/column_int64/column_double/column_text/column_blob/column_optional<T>` (0 始まり)、
  `run()`、`first(read)`、`for_each(fn)`
- `Transaction(db, mode = immediate)` — `commit()` / `rollback()`、破棄で ROLLBACK
- `Error` — `code()` (一次コード) / `extended_code()`。`lapilli::sqlite::code::busy` などの定数あり

## 守っていること

- すべての `sqlite3_*` の戻り値を確かめ、失敗は `Error` で投げる。
- 文字列と blob は `SQLITE_TRANSIENT` で bind する (SQLite がその場で複製する)。bind した後に
  元の文字列を捨ててよい。
- `prepare` は文を 1 つだけ受ける。2 つ目の文があれば Error (黙って無視しない)。
- `bind_all` は渡した数が文のパラメータ数と違えば Error (渡し忘れが NULL にならない)。
- `run` / `first` / `for_each` は終わったら (例外でも) 文を巻き戻す。途中で止めた SELECT が
  読み取りのスナップショットを握り続けない。bind は残る。`reset()` は bind も消す。
- `step` が失敗したら文を巻き戻してから投げる。制約違反の後も DB と文はそのまま使える。
- `table_columns` のテーブル名は bind できないので、`[A-Za-z_][A-Za-z0-9_]*` (128 文字まで) で
  なければ Error。値を SQL に埋め込む補助は作らない。
- `bool` は bind しない (コンパイルエラー)。整数として明示的に渡す。

## 並行性

- **1 つの `Database` は 1 つのスレッドで使う。** このラッパーはロックを持たず、接続をスレッド間で
  共有する仕組みも持たない。スレッドごとに `Database::open` する。
- 別プロセス (Memoria の Node サーバなど) と同じ DB を使うときは WAL にする。WAL なら読み手は
  書き手を待たない。書き手は同時に 1 つだけ。
- ロック待ちは `busy_timeout_ms` まで。過ぎたら `code() == code::busy` の Error が返る
  (止まり続けない)。呼び出し側で再試行するか、失敗として扱う。
- 書くトランザクションは `Transaction` の既定 (`BEGIN IMMEDIATE`) で始める。`DEFERRED` は読みから
  書きへ上げるときに busy handler を待たずに `SQLITE_BUSY` になりうる。

## 寿命

- `Statement` を `Database` より長く生かさない。生かしてしまった場合、その `Statement` を使うと
  `SQLITE_MISUSE` の Error になる (落ちない)。破棄は安全 (`sqlite3_close_v2` を使うので、残った文の
  finalize まで接続の実体が残る)。
- `Database` は移動できる。移動しても接続は同じなので、作った `Statement` はそのまま使える。
- `Transaction` は接続を共有して持つ。`Database` を `close()` しても、`Transaction` の破棄
  (ROLLBACK) までは接続が残る。
