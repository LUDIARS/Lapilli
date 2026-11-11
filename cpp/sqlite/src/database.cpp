#include <climits>

#include "internal.hpp"

namespace lapilli::sqlite {

namespace detail {

Connection::~Connection() {
	//close_v2 は残った文があっても失敗しない (文の finalize まで実体を残す)。破棄では戻り値を使い道が無い
	if (db) sqlite3_close_v2(db);
}

}

Database::Database(std::shared_ptr<detail::Connection> conn) : conn_(std::move(conn)) {}
Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

detail::Connection& Database::connection() const {
	if (!conn_) detail::throw_misuse("database is closed");
	return *conn_;
}

Database Database::open(const std::string& path, const OpenOptions& options) {
	if (path.find('\0') != std::string::npos) detail::throw_misuse("open: path contains NUL");
	if (options.read_only && options.create) detail::throw_misuse("open: read_only and create cannot be combined");
	if (options.busy_timeout_ms < 0) detail::throw_misuse("open: busy_timeout_ms must not be negative");

	int flags = options.read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
	if (options.create) flags |= SQLITE_OPEN_CREATE;

	sqlite3* raw = nullptr;
	const int rc = sqlite3_open_v2(path.c_str(), &raw, flags, nullptr);
	//失敗しても handle は返ることがあるので、先に持ち主を決めて必ず閉じる
	auto conn = std::make_shared<detail::Connection>(raw);
	if (rc != SQLITE_OK) {
		if (!raw) throw Error(rc, "lapilli::sqlite: open " + path + ": out of memory");
		std::string what = "open " + path;
		if ((rc & 0xff) == SQLITE_CANTOPEN && !options.create) what += " (the file must already exist because create=false)";
		detail::throw_error(raw, rc, what);
	}
	//以後の戻り値は拡張コード (SQLITE_CONSTRAINT_UNIQUE など) で受ける
	if (const int erc = sqlite3_extended_result_codes(raw, 1); erc != SQLITE_OK) detail::throw_error(raw, erc, "open: extended result codes");
	//ロック待ちは SQLite の busy handler に任せる。上限を過ぎると SQLITE_BUSY が返り、Error になる (止まり続けない)
	if (const int brc = sqlite3_busy_timeout(raw, options.busy_timeout_ms); brc != SQLITE_OK) detail::throw_error(raw, brc, "open: busy timeout");

	Database db(std::move(conn));
	db.exec(options.foreign_keys ? "PRAGMA foreign_keys = ON" : "PRAGMA foreign_keys = OFF");
	if (options.wal) {
		//journal_mode は変えられなくても失敗を返さず今のモードを返すので、結果を見て確かめる
		Statement s = db.prepare("PRAGMA journal_mode = WAL");
		const std::optional<std::string> mode = s.first([](const Statement& row) { return row.column_text(0); });
		if (!mode || (*mode != "wal" && *mode != "WAL")) {
			throw Error(SQLITE_ERROR, "lapilli::sqlite: open " + path + ": could not enable WAL (journal_mode is " + mode.value_or("unknown") + ")");
		}
	}
	return db;
}

void Database::exec(const std::string& sql) {
	sqlite3* db = connection().db;
	if (sql.find('\0') != std::string::npos) detail::throw_misuse("exec: SQL contains NUL");
	char* err = nullptr;
	const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
	if (rc == SQLITE_OK) return;
	std::string message = "lapilli::sqlite: exec: ";
	message += err ? err : sqlite3_errstr(rc);
	message += " (code " + std::to_string(rc) + ")";
	sqlite3_free(err);
	throw Error(rc, message);
}

Statement Database::prepare(std::string_view sql) {
	sqlite3* db = connection().db;
	if (sql.size() > static_cast<std::size_t>(INT_MAX)) detail::throw_misuse("prepare: SQL too long");
	sqlite3_stmt* stmt = nullptr;
	const char* tail = nullptr;
	const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);
	if (rc != SQLITE_OK) detail::throw_error(db, rc, "prepare");
	//空白やコメントだけの SQL は文にならない
	if (!stmt) detail::throw_misuse("prepare: SQL contains no statement");
	auto impl = std::make_unique<detail::StatementImpl>(conn_, stmt);

	//残りに文があれば拒む。残りを試しに準備して、文が出てくるか (または壊れているか) で決める
	const std::size_t used = static_cast<std::size_t>(tail - sql.data());
	if (used < sql.size()) {
		sqlite3_stmt* extra = nullptr;
		const int trc = sqlite3_prepare_v2(db, tail, static_cast<int>(sql.size() - used), &extra, nullptr);
		if (extra) sqlite3_finalize(extra);
		if (trc != SQLITE_OK) detail::throw_error(db, trc, "prepare (after the first statement)");
		if (extra) detail::throw_misuse("prepare: SQL contains more than one statement");
	}
	return Statement(std::move(impl));
}

std::int64_t Database::last_insert_rowid() const {
	return static_cast<std::int64_t>(sqlite3_last_insert_rowid(connection().db));
}

std::int64_t Database::changes() const {
#if SQLITE_VERSION_NUMBER >= 3037000
	return static_cast<std::int64_t>(sqlite3_changes64(connection().db));
#else
	return static_cast<std::int64_t>(sqlite3_changes(connection().db));
#endif
}

std::vector<std::string> Database::table_columns(std::string_view table) {
	//PRAGMA の引数は bind できないので、識別子だけを通してから引用符で囲んで組み立てる
	if (!detail::is_identifier(table)) detail::throw_misuse("table_columns: table name is not a plain identifier");
	std::string sql = "PRAGMA table_info(\"";
	sql.append(table);
	sql += "\")";
	Statement s = prepare(sql);
	std::vector<std::string> names;
	s.for_each([&](const Statement& row) { names.push_back(row.column_text(1)); });
	return names;
}

void Database::close() noexcept {
	conn_.reset();
}

}
