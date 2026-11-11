#include <stdexcept>

#include <sqlite3.h>

#include "test.hpp"

using namespace lapilli::sqlite;

namespace {

std::int64_t count_rows(Database& db) {
	Statement s = db.prepare("SELECT count(*) FROM t");
	return s.first([](const Statement& r) { return r.column_int64(0); }).value_or(-1);
}

}

TEST(transaction_commit, "Transaction: commit で残る") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (v)");
	{
		Transaction tx(db);
		Statement ins = db.prepare("INSERT INTO t VALUES (?)");
		for (int i = 0; i < 3; ++i) ins.bind_all(i).run();
		tx.commit();
		CHECK_SQLITE_ERROR(tx.commit(), SQLITE_MISUSE);
	}
	CHECK_EQ(count_rows(db), 3);
}

TEST(transaction_rollback_on_exception, "Transaction: 例外で抜けたら ROLLBACK され、接続は次のトランザクションに使える") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (v INTEGER UNIQUE)");
	bool threw = false;
	try {
		Transaction tx(db);
		Statement ins = db.prepare("INSERT INTO t VALUES (?)");
		ins.bind_all(1).run();
		ins.bind_all(2).run();
		ins.bind_all(1).run();	//UNIQUE 違反で投げる
		tx.commit();
	} catch (const Error& e) {
		threw = true;
		CHECK_EQ(e.code(), SQLITE_CONSTRAINT);
	}
	CHECK(threw);
	CHECK_EQ(count_rows(db), 0);

	try {
		Transaction tx(db, TransactionMode::deferred);
		db.exec("INSERT INTO t VALUES (5)");
		throw std::runtime_error("application error");
	} catch (const std::runtime_error&) {
	}
	CHECK_EQ(count_rows(db), 0);

	{
		Transaction tx(db, TransactionMode::exclusive);
		db.exec("INSERT INTO t VALUES (9)");
		tx.rollback();
		CHECK_SQLITE_ERROR(tx.rollback(), SQLITE_MISUSE);
	}
	CHECK_EQ(count_rows(db), 0);

	{
		Transaction tx(db);
		db.exec("INSERT INTO t VALUES (7)");
		tx.commit();
	}
	CHECK_EQ(count_rows(db), 1);
}

TEST(transaction_nested_and_closed, "Transaction: 入れ子の BEGIN は Error、閉じた DB では作れない") {
	testing::TempPath path;
	Database db = testing::open_new(path);
	db.exec("CREATE TABLE t (v)");
	{
		Transaction outer(db);
		CHECK_SQLITE_ERROR(Transaction(db), SQLITE_ERROR);
		db.exec("INSERT INTO t VALUES (1)");
		outer.commit();
	}
	CHECK_EQ(count_rows(db), 1);
	db.close();
	CHECK_SQLITE_ERROR(Transaction(db), SQLITE_MISUSE);
}

TEST(transaction_outlives_close, "Transaction: Database を close しても破棄までは接続が残り、ROLLBACK される") {
	testing::TempPath path;
	{
		Database db = testing::open_new(path);
		db.exec("CREATE TABLE t (v)");
		Transaction tx(db);
		db.exec("INSERT INTO t VALUES (1)");
		db.close();
	}
	Database db = Database::open(path.str());
	CHECK_EQ(count_rows(db), 0);
}
