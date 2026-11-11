#include "internal.hpp"

namespace lapilli::sqlite {

namespace {

const char* begin_sql(TransactionMode mode) {
	switch (mode) {
	case TransactionMode::deferred:
		return "BEGIN DEFERRED";
	case TransactionMode::immediate:
		return "BEGIN IMMEDIATE";
	case TransactionMode::exclusive:
		return "BEGIN EXCLUSIVE";
	}
	detail::throw_misuse("transaction: unknown mode");
}

void run(sqlite3* db, const char* sql) {
	char* err = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
	if (rc == SQLITE_OK) return;
	std::string message = "lapilli::sqlite: ";
	message += sql;
	message += ": ";
	message += err ? err : sqlite3_errstr(rc);
	message += " (code " + std::to_string(rc) + ")";
	sqlite3_free(err);
	throw Error(rc, message);
}

//SQLite が自分で戻した場合 (SQLITE_FULL など) もあるので、まだ中にいるかを見る
bool in_transaction(sqlite3* db) {
	return sqlite3_get_autocommit(db) == 0;
}

}

Transaction::Transaction(Database& db, TransactionMode mode) : conn_(db.conn_) {
	if (!conn_) detail::throw_misuse("transaction: database is closed");
	run(conn_->db, begin_sql(mode));
}

Transaction::~Transaction() {
	if (!active_ || !in_transaction(conn_->db)) return;
	//破棄 (例外の巻き戻し中を含む) では投げない。ROLLBACK の失敗は接続側に残る状態で分かる
	sqlite3_exec(conn_->db, "ROLLBACK", nullptr, nullptr, nullptr);
}

void Transaction::commit() {
	if (!active_) detail::throw_misuse("transaction: already finished");
	run(conn_->db, "COMMIT");
	active_ = false;
}

void Transaction::rollback() {
	if (!active_) detail::throw_misuse("transaction: already finished");
	active_ = false;
	if (in_transaction(conn_->db)) run(conn_->db, "ROLLBACK");
}

}
