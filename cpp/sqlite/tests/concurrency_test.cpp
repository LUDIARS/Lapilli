//WAL で 2 本の接続 (別プロセスの better-sqlite3 と同じ DB を使う状況の再現)
#include <atomic>
#include <chrono>
#include <thread>

#include <sqlite3.h>

#include "test.hpp"

using namespace lapilli::sqlite;

namespace {

OpenOptions wal_options(int busy_timeout_ms) {
	OpenOptions o;
	o.wal = true;
	o.busy_timeout_ms = busy_timeout_ms;
	return o;
}

void create_wal_db(const testing::TempPath& path) {
	OpenOptions o = wal_options(1000);
	o.create = true;
	Database db = Database::open(path.str(), o);
	db.exec("CREATE TABLE t (v INTEGER); INSERT INTO t VALUES (1)");
}

std::int64_t count_rows(Database& db) {
	Statement s = db.prepare("SELECT count(*) FROM t");
	return s.first([](const Statement& r) { return r.column_int64(0); }).value_or(-1);
}

long long elapsed_ms(std::chrono::steady_clock::time_point since) {
	return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count());
}

}

TEST(wal_busy_same_thread, "WAL: 片方が書き込み中なら、もう片方の書き込みは busy_timeout の後に SQLITE_BUSY (止まり続けない)。読むのは待たない") {
	testing::TempPath path;
	create_wal_db(path);
	Database writer = Database::open(path.str(), wal_options(1000));
	Database other = Database::open(path.str(), wal_options(150));

	Transaction tx(writer);
	writer.exec("INSERT INTO t VALUES (2)");

	//WAL なので読み手は書き手を待たない (コミット前の行は見えない)
	CHECK_EQ(count_rows(other), 1);

	const auto start = std::chrono::steady_clock::now();
	CHECK_SQLITE_ERROR(other.exec("INSERT INTO t VALUES (3)"), SQLITE_BUSY);
	const long long waited = elapsed_ms(start);
	CHECK(waited >= 100);	//busy_timeout の分は待っている
	CHECK(waited < 5000);	//止まり続けてはいない
	CHECK_SQLITE_ERROR(Transaction(other), SQLITE_BUSY);
	Statement ins = other.prepare("INSERT INTO t VALUES (?)");
	CHECK_SQLITE_ERROR(ins.bind_all(4).run(), SQLITE_BUSY);

	tx.commit();
	//ロックが外れれば同じ文で書ける
	ins.run();
	CHECK_EQ(count_rows(other), 3);
	CHECK_EQ(count_rows(writer), 3);
}

TEST(wal_busy_two_threads, "WAL: スレッドごとに Database を開く。書き手スレッドが握っている間、別スレッドの書き込みは BUSY で返り、離せば通る") {
	testing::TempPath path;
	create_wal_db(path);

	std::atomic<bool> holding{ false };
	std::atomic<bool> release{ false };
	std::atomic<bool> writer_ok{ false };
	std::thread writer([&] {
		try {
			Database db = Database::open(path.str(), wal_options(1000));
			Transaction tx(db);
			db.exec("INSERT INTO t VALUES (2)");
			holding = true;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
			tx.commit();
			writer_ok = true;
		} catch (...) {
			holding = true;	//待っている側を止めないように
		}
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!holding && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
	CHECK(holding.load());

	int busy_code = -1;
	long long waited = -1;
	std::thread contender([&] {
		try {
			Database db = Database::open(path.str(), wal_options(150));
			const auto start = std::chrono::steady_clock::now();
			try {
				db.exec("INSERT INTO t VALUES (3)");
			} catch (const Error& e) {
				busy_code = e.code();
			}
			waited = elapsed_ms(start);
		} catch (...) {
		}
	});
	contender.join();
	CHECK_EQ(busy_code, SQLITE_BUSY);
	CHECK(waited >= 100 && waited < 5000);

	release = true;
	writer.join();
	CHECK(writer_ok.load());

	//書き手が離した後は、長めの busy_timeout で待つ側も通る
	std::thread after([&] {
		try {
			Database db = Database::open(path.str(), wal_options(2000));
			db.exec("INSERT INTO t VALUES (3)");
		} catch (...) {
			busy_code = -2;
		}
	});
	after.join();
	CHECK(busy_code != -2);
	Database check = Database::open(path.str());
	CHECK_EQ(count_rows(check), 3);
}
