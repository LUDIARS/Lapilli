#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "test.hpp"

using namespace lapilli::sqlite;

namespace {

//列の型を SQLite に聞く (bind が意図した型で入ったかを確かめる)
std::string type_of(Database& db, std::int64_t id, const char* column) {
	Statement s = db.prepare(std::string("SELECT typeof(") + column + ") FROM t WHERE id = ?");
	s.bind_all(id);
	return s.first([](const Statement& r) { return r.column_text(0); }).value_or("missing");
}

}

TEST(round_trip_types, "bind / column: int64・double・UTF-8 text・blob (途中 NUL)・NULL・optional の往復") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, i INTEGER, r REAL, s TEXT, b BLOB, n)");

	const std::string utf8 = u8"東京都 🗼 ü";
	const std::string text_nul("a\0b", 3);
	const std::vector<std::uint8_t> blob = { 0x00, 0xff, 0x00, 0x10, 0x00 };
	Statement ins = db.prepare("INSERT INTO t (id, i, r, s, b, n) VALUES (?, ?, ?, ?, ?, ?)");
	ins.bind_all(1, std::numeric_limits<std::int64_t>::min(), 3.25, utf8, blob, nullptr).run();
	ins.bind_all(2, std::numeric_limits<std::int64_t>::max(), -0.5, text_nul, BlobView(), std::nullopt).run();
	ins.bind_all(3, std::optional<int>(7), std::optional<double>(), std::optional<std::string>("opt"), std::optional<std::string>(), 0u).run();
	ins.bind_all(4, 5, 1e300, "", std::vector<std::uint8_t>(), std::string_view("sv")).run();

	Statement sel = db.prepare("SELECT i, r, s, b, n FROM t WHERE id = ?");

	sel.bind_all(1);
	CHECK(sel.step());
	CHECK_EQ(sel.column_count(), 5);
	CHECK_EQ(sel.column_name(0), "i");
	CHECK_EQ(sel.column_int64(0), std::numeric_limits<std::int64_t>::min());
	CHECK_EQ(sel.column_double(1), 3.25);
	CHECK_EQ(sel.column_text(2), utf8);
	CHECK(sel.column_blob(3) == blob);
	CHECK(sel.column_is_null(4));
	CHECK(!sel.column_optional<std::int64_t>(4).has_value());
	CHECK(!sel.step());
	CHECK_EQ(type_of(db, 1, "b"), "blob");
	CHECK_EQ(type_of(db, 1, "n"), "null");

	sel.reset();
	sel.bind_all(2);
	CHECK(sel.step());
	CHECK_EQ(sel.column_int64(0), std::numeric_limits<std::int64_t>::max());
	CHECK_EQ(sel.column_double(1), -0.5);
	CHECK_EQ(sel.column_text(2), text_nul);
	CHECK_EQ(sel.column_text(2).size(), 3u);
	CHECK(sel.column_blob(3).empty());
	CHECK(!sel.column_is_null(3));	//長さ 0 の blob は NULL ではない
	CHECK(sel.column_is_null(4));
	sel.reset();
	CHECK_EQ(type_of(db, 2, "b"), "blob");

	sel.bind_all(3);
	CHECK(sel.step());
	CHECK_EQ(sel.column_int64(0), 7);
	CHECK(sel.column_is_null(1));
	CHECK(sel.column_optional<std::string>(2) == std::optional<std::string>("opt"));
	CHECK(!sel.column_optional<std::vector<std::uint8_t>>(3).has_value());
	CHECK(sel.column_optional<std::int64_t>(4) == std::optional<std::int64_t>(0));
	sel.reset();

	sel.bind_all(4);
	CHECK(sel.step());
	CHECK_EQ(sel.column_double(1), 1e300);
	CHECK(!sel.column_is_null(2));	//空文字は NULL ではない
	CHECK_EQ(sel.column_text(2), "");
	CHECK(sel.column_optional<double>(1) == std::optional<double>(1e300));
	CHECK_EQ(sel.column_text(4), "sv");
	sel.reset();
	CHECK_EQ(type_of(db, 4, "s"), "text");
	CHECK_EQ(type_of(db, 4, "b"), "blob");
}

TEST(unsigned_overflow, "bind: int64 に収まらない unsigned は Error") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	Statement s = db.prepare("SELECT ?");
	CHECK_SQLITE_ERROR(s.bind(1, std::numeric_limits<std::uint64_t>::max()), SQLITE_MISMATCH);
	s.bind(1, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
	CHECK(s.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(std::numeric_limits<std::int64_t>::max()));
}

TEST(text_bind_lifetime, "bind_text: 一時文字列を bind して破棄してから step しても値が残る") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (s TEXT, b BLOB)");
	Statement ins = db.prepare("INSERT INTO t VALUES (?, ?)");
	{
		std::string temp(4096, 'x');
		temp.replace(0, 5, "start");
		std::vector<std::uint8_t> bytes(1000, 0xab);
		ins.bind(1, temp);
		ins.bind(2, bytes);
		//元を壊してから捨てる (SQLite が複製していなければ壊れた値か解放済みの領域を読む)
		temp.assign(4096, '#');
		bytes.assign(1000, 0);
	}
	ins.bind(1, std::string("short-lived temporary"));
	ins.bind(1, std::string(4096, 'x').replace(0, 5, "start"));
	ins.run();
	Statement sel = db.prepare("SELECT s, b FROM t");
	const auto row = sel.first([](const Statement& r) { return std::make_pair(r.column_text(0), r.column_blob(1)); });
	CHECK(row.has_value());
	if (row) {
		CHECK_EQ(row->first.size(), 4096u);
		CHECK_EQ(row->first.substr(0, 6), "startx");
		CHECK(row->second == std::vector<std::uint8_t>(1000, 0xab));
	}
}

TEST(reset_reuse, "reset: 巻き戻して bind も消える。同じ文を何度も使える") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");
	Statement ins = db.prepare("INSERT INTO t (v) VALUES (?)");
	for (int i = 0; i < 5; ++i) ins.bind_all("v" + std::to_string(i)).run();

	Statement sel = db.prepare("SELECT v FROM t WHERE id > ? ORDER BY id");
	sel.bind(1, 3);
	std::vector<std::string> got;
	while (sel.step()) got.push_back(sel.column_text(0));
	CHECK((got == std::vector<std::string>{ "v3", "v4" }));

	//実行後の文には reset しないと bind できない
	CHECK_SQLITE_ERROR(sel.bind(1, 0), SQLITE_MISUSE);
	sel.reset();
	//reset で bind が消える: id > NULL は偽なので 0 行
	CHECK(!sel.step());
	sel.reset();
	sel.bind(1, 0);
	got.clear();
	sel.for_each([&](const Statement& r) { got.push_back(r.column_text(0)); });
	CHECK_EQ(got.size(), 5u);
	//for_each の後は巻き戻っていて、bind は残っている
	got.clear();
	sel.for_each([&](const Statement& r) { got.push_back(r.column_text(0)); });
	CHECK_EQ(got.size(), 5u);
	//途中で止めた first の後も巻き戻っている
	CHECK(sel.first([](const Statement& r) { return r.column_text(0); }) == std::optional<std::string>("v0"));
	CHECK(sel.first([](const Statement& r) { return r.column_text(0); }) == std::optional<std::string>("v0"));
}

TEST(misuse_guards, "行が無いときの列読み・範囲外の列・パラメータ数の不一致・run で行が返る文") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	Statement s = db.prepare("SELECT ?, ?");
	CHECK_SQLITE_ERROR(s.column_int64(0), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(s.bind_all(1), SQLITE_RANGE);
	CHECK_SQLITE_ERROR(s.bind_all(1, 2, 3), SQLITE_RANGE);
	CHECK_SQLITE_ERROR(s.bind(3, 1), SQLITE_RANGE);
	CHECK_SQLITE_ERROR(s.bind(0, 1), SQLITE_RANGE);
	s.bind_all(1, 2);
	CHECK(s.step());
	CHECK_SQLITE_ERROR(s.column_int64(2), SQLITE_RANGE);
	CHECK_SQLITE_ERROR(s.column_text(-1), SQLITE_RANGE);
	CHECK_SQLITE_ERROR(s.column_name(9), SQLITE_RANGE);
	CHECK(!s.step());
	CHECK_SQLITE_ERROR(s.column_int64(0), SQLITE_MISUSE);
	CHECK_SQLITE_ERROR(s.run(), SQLITE_MISUSE);

	Statement moved = std::move(s);
	CHECK_SQLITE_ERROR(s.step(), SQLITE_MISUSE);
	CHECK(moved.first([](const Statement& r) { return r.column_int64(1); }) == std::optional<std::int64_t>(2));
}

TEST(first_and_for_each_exception, "for_each の中で投げても文は巻き戻る") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (v); INSERT INTO t VALUES (1), (2), (3)");
	Statement s = db.prepare("SELECT v FROM t ORDER BY v");
	int seen = 0;
	CHECK_THROWS(s.for_each([&](const Statement&) {
		++seen;
		throw std::runtime_error("stop");
	}));
	CHECK_EQ(seen, 1);
	//巻き戻っていなければ 2 が返る
	CHECK(s.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(1));
	Statement none = db.prepare("SELECT v FROM t WHERE v > 10");
	CHECK(!none.first([](const Statement& r) { return r.column_int64(0); }).has_value());
}

TEST(constraint_violation, "制約違反は SQLITE_CONSTRAINT (拡張コード付き) の Error で、DB と文はそのまま使える") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, k TEXT UNIQUE NOT NULL)");
	Statement ins = db.prepare("INSERT INTO t (k) VALUES (?)");
	ins.bind_all("a").run();
	bool threw = false;
	try {
		ins.bind_all("a").run();
	} catch (const Error& e) {
		threw = true;
		CHECK_EQ(e.code(), SQLITE_CONSTRAINT);
		CHECK_EQ(e.extended_code(), SQLITE_CONSTRAINT_UNIQUE);
		CHECK(std::string(e.what()).find("UNIQUE") != std::string::npos);
	}
	CHECK(threw);
	try {
		ins.bind_all(nullptr).run();
		CHECK(false);
	} catch (const Error& e) {
		CHECK_EQ(e.extended_code(), SQLITE_CONSTRAINT_NOTNULL);
	}
	//同じ文も、別の文も使える
	ins.bind_all("b").run();
	db.exec("INSERT INTO t (k) VALUES ('c')");
	Statement count = db.prepare("SELECT count(*) FROM t");
	CHECK(count.first([](const Statement& r) { return r.column_int64(0); }) == std::optional<std::int64_t>(3));
}
