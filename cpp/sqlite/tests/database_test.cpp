#include <filesystem>

#include <sqlite3.h>

#include "test.hpp"

using namespace lapilli::sqlite;

TEST(open_missing, "open: create=false で無いファイルは開かずに失敗し、ファイルも作らない") {
	testing::TempPath path;
	bool threw = false;
	try {
		Database::open(path.str());
	} catch (const Error& e) {
		threw = true;
		CHECK_EQ(e.code(), SQLITE_CANTOPEN);
		CHECK(std::string(e.what()).find("create=false") != std::string::npos);
	}
	CHECK(threw);
	CHECK(!std::filesystem::exists(path.str()));

	OpenOptions ro;
	ro.read_only = true;
	CHECK_SQLITE_ERROR(Database::open(path.str(), ro), SQLITE_CANTOPEN);
}

TEST(open_options_misuse, "open: 矛盾する指定は Error") {
	testing::TempPath path;
	OpenOptions o;
	o.read_only = true;
	o.create = true;
	CHECK_SQLITE_ERROR(Database::open(path.str(), o), SQLITE_MISUSE);
	OpenOptions neg;
	neg.create = true;
	neg.busy_timeout_ms = -1;
	CHECK_SQLITE_ERROR(Database::open(path.str(), neg), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(Database::open(std::string("a\0b", 3), OpenOptions()), SQLITE_MISUSE);
}

TEST(foreign_keys_default, "open: foreign_keys は既定で有効、false で無効") {
	testing::TempPath path;
	{
		Database db = testing::open_new(path);
		db.exec("CREATE TABLE p (id INTEGER PRIMARY KEY); CREATE TABLE c (pid INTEGER REFERENCES p(id))");
		CHECK_SQLITE_ERROR(db.exec("INSERT INTO c VALUES (42)"), SQLITE_CONSTRAINT);
	}
	OpenOptions off;
	off.foreign_keys = false;
	Database db = Database::open(path.str(), off);
	db.exec("INSERT INTO c VALUES (42)");
	CHECK_EQ(db.changes(), 1);
}

TEST(exec_errors, "exec: 構文エラーは Error (code と sqlite のメッセージ付き)") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	bool threw = false;
	try {
		db.exec("CREAT TABLE x (a)");
	} catch (const Error& e) {
		threw = true;
		CHECK_EQ(e.code(), SQLITE_ERROR);
		CHECK(std::string(e.what()).find("syntax error") != std::string::npos);
	}
	CHECK(threw);
}

TEST(prepare_rejects_multiple, "prepare: 2 つ目の文があれば拒む (コメントと空白は可)、空の SQL も拒む") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (a)");
	CHECK_SQLITE_ERROR(db.prepare("SELECT 1; DELETE FROM t"), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(db.prepare("SELECT 1; garbage"), SQLITE_ERROR);
	CHECK_SQLITE_ERROR(db.prepare("   -- only a comment"), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(db.prepare(""), SQLITE_MISUSE);
	Statement ok = db.prepare("SELECT 1;  -- trailing comment\n");
	CHECK(ok.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(1));
	CHECK_SQLITE_ERROR(db.prepare("SELECT * FROM missing_table"), SQLITE_ERROR);
}

TEST(rowid_and_changes, "last_insert_rowid / changes") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
	Statement ins = db.prepare("INSERT INTO t (v) VALUES (?)");
	const RunResult a = ins.bind_all(10).run();
	CHECK_EQ(a.last_insert_rowid, 1);
	CHECK_EQ(a.changes, 1);
	const RunResult b = ins.bind_all(20).run();
	CHECK_EQ(b.last_insert_rowid, 2);
	CHECK_EQ(db.last_insert_rowid(), 2);
	db.exec("INSERT INTO t (id, v) VALUES (100, 30)");
	CHECK_EQ(db.last_insert_rowid(), 100);

	Statement upd = db.prepare("UPDATE t SET v = v + 1 WHERE v >= ?");
	CHECK_EQ(upd.bind_all(20).run().changes, 2);
	CHECK_EQ(db.changes(), 2);
}

TEST(table_columns, "table_columns: 定義順の列名、無いテーブルは空、識別子でない名前は拒む") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE gps (id INTEGER PRIMARY KEY, lat REAL, lon REAL)");
	db.exec("ALTER TABLE gps ADD COLUMN place_name TEXT");
	const std::vector<std::string> cols = db.table_columns("gps");
	CHECK((cols == std::vector<std::string>{ "id", "lat", "lon", "place_name" }));
	CHECK(db.table_columns("no_such_table").empty());

	for (const char* hostile : { "gps\"); DROP TABLE gps; --", "gps)", "", "1abc", "a b", "gps;", "\"gps\"", "main.gps", "ｇｐｓ" }) {
		CHECK_SQLITE_ERROR(db.table_columns(hostile), SQLITE_MISUSE);
	}
	CHECK_SQLITE_ERROR(db.table_columns(std::string(129, 'a')), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(db.table_columns(std::string("gps\0x", 5)), SQLITE_MISUSE);
	//何も消えていない
	CHECK_EQ(db.table_columns("gps").size(), 4u);
}

TEST(read_only, "read_only: 読めるが書けない (SQLITE_READONLY)") {
	testing::TempPath path;
	{
		Database db = testing::open_new(path);
		db.exec("CREATE TABLE t (v); INSERT INTO t VALUES (1)");
	}
	OpenOptions ro;
	ro.read_only = true;
	Database db = Database::open(path.str(), ro);
	Statement s = db.prepare("SELECT count(*) FROM t");
	CHECK(s.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(1));
	CHECK_SQLITE_ERROR(db.exec("INSERT INTO t VALUES (2)"), SQLITE_READONLY);
	Statement ins = db.prepare("INSERT INTO t VALUES (?)");
	CHECK_SQLITE_ERROR(ins.bind_all(3).run(), SQLITE_READONLY);
}

TEST(close_and_move, "close / 移動後の Database と、Database より長生きした Statement は Error (落ちない)") {
	testing::TempPath path;
	Statement orphan = [&] {
		Database db = testing::open_new(path);
		db.exec("CREATE TABLE t (v)");
		return db.prepare("SELECT v FROM t");
	}();
	CHECK_SQLITE_ERROR(orphan.step(), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(orphan.bind_null(1), SQLITE_MISUSE);

	Database a = Database::open(path.str());
	Statement s = a.prepare("SELECT count(*) FROM t");
	Database b = std::move(a);
	CHECK(!a.is_open());
	CHECK(b.is_open());
	CHECK_SQLITE_ERROR(a.exec("SELECT 1"), SQLITE_MISUSE);
	//移動しても接続は同じなので、文はそのまま使える
	CHECK(s.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(0));
	b.close();
	CHECK(!b.is_open());
	CHECK_SQLITE_ERROR(s.step(), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(b.prepare("SELECT 1"), SQLITE_MISUSE);
}

TEST(wal_mode, "wal=true: journal_mode が WAL になり、ファイルに残る") {
	testing::TempPath path;
	OpenOptions o;
	o.create = true;
	o.wal = true;
	{
		Database db = Database::open(path.str(), o);
		db.exec("CREATE TABLE t (v)");
	}
	Database db = Database::open(path.str());
	Statement s = db.prepare("PRAGMA journal_mode");
	CHECK(s.first([](const Statement& r) { return r.column_text(0); }) == std::optional<std::string>("wal"));
}
