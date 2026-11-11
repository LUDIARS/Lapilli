#include "internal.hpp"

namespace lapilli::sqlite {

namespace detail {

StatementImpl::~StatementImpl() {
	//接続が先に閉じられていても close_v2 のおかげで finalize は有効。戻り値は直前の step の失敗を繰り返すだけなので見ない
	if (stmt) sqlite3_finalize(stmt);
}

}

namespace {

//接続が生きていることを確かめて、その handle を返す
sqlite3* live(const std::unique_ptr<detail::StatementImpl>& impl) {
	if (!impl) detail::throw_misuse("statement is empty (moved from)");
	if (impl->conn.expired()) detail::throw_misuse("statement used after its Database was closed");
	return sqlite3_db_handle(impl->stmt);
}

void check_bind(sqlite3* db, int rc, int index) {
	if (rc == SQLITE_OK) return;
	//実行中・実行後の文には bind できない (SQLITE_MISUSE)。reset() してから bind する
	detail::throw_error(db, rc, "bind parameter " + std::to_string(index));
}

}

Statement::Statement(std::unique_ptr<detail::StatementImpl> impl) : impl_(std::move(impl)) {}
Statement::~Statement() = default;
Statement::Statement(Statement&&) noexcept = default;
Statement& Statement::operator=(Statement&&) noexcept = default;

Statement& Statement::bind_int64(int index, std::int64_t value) {
	sqlite3* db = live(impl_);
	check_bind(db, sqlite3_bind_int64(impl_->stmt, index, static_cast<sqlite3_int64>(value)), index);
	return *this;
}

Statement& Statement::bind_double(int index, double value) {
	sqlite3* db = live(impl_);
	check_bind(db, sqlite3_bind_double(impl_->stmt, index, value), index);
	return *this;
}

Statement& Statement::bind_text(int index, std::string_view value) {
	sqlite3* db = live(impl_);
	//SQLITE_TRANSIENT: SQLite がその場で複製するので、呼び出し側の文字列が先に消えても宙に浮かない
	//  空の string_view は data() が nullptr のことがあり、nullptr を渡すと NULL になってしまうので "" を渡す
	const char* data = value.empty() ? "" : value.data();
	check_bind(db, sqlite3_bind_text64(impl_->stmt, index, data, static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8), index);
	return *this;
}

Statement& Statement::bind_blob(int index, BlobView value) {
	sqlite3* db = live(impl_);
	if (value.size == 0) {
		//nullptr の blob は NULL になる。長さ 0 の blob は zeroblob(0) で表す
		check_bind(db, sqlite3_bind_zeroblob(impl_->stmt, index, 0), index);
		return *this;
	}
	if (!value.data) detail::throw_misuse("bind_blob: data is null but size is not zero");
	check_bind(db, sqlite3_bind_blob64(impl_->stmt, index, value.data, static_cast<sqlite3_uint64>(value.size), SQLITE_TRANSIENT), index);
	return *this;
}

Statement& Statement::bind_null(int index) {
	sqlite3* db = live(impl_);
	check_bind(db, sqlite3_bind_null(impl_->stmt, index), index);
	return *this;
}

void Statement::check_parameter_count(int given) const {
	live(impl_);
	const int expected = sqlite3_bind_parameter_count(impl_->stmt);
	if (expected != given) {
		throw Error(SQLITE_RANGE, "lapilli::sqlite: bind_all: statement has " + std::to_string(expected) + " parameters but " + std::to_string(given) + " values were given");
	}
}

bool Statement::step() {
	sqlite3* db = live(impl_);
	impl_->has_row = false;
	const int rc = sqlite3_step(impl_->stmt);
	if (rc == SQLITE_ROW) {
		impl_->has_row = true;
		return true;
	}
	if (rc == SQLITE_DONE) return false;
	//失敗した文は巻き戻しておく (読み取りのロックを握ったままにせず、bind を変えて再利用できるように)
	//  メッセージは巻き戻しで上書きされる前に作る
	const std::string message = detail::describe(db, rc, "step");
	sqlite3_reset(impl_->stmt);
	throw Error(rc, message);
}

void Statement::reset() {
	live(impl_);
	impl_->has_row = false;
	//sqlite3_reset の戻り値は直前の step の失敗を繰り返すだけ (それは step が投げ済み)
	sqlite3_reset(impl_->stmt);
	sqlite3_clear_bindings(impl_->stmt);
}

void Statement::rewind() noexcept {
	if (!impl_) return;
	impl_->has_row = false;
	sqlite3_reset(impl_->stmt);
}

RunResult Statement::run() {
	Rewind guard(*this);
	rewind();
	if (step()) detail::throw_misuse("run: statement returned a row (use first or for_each)");
	sqlite3* db = sqlite3_db_handle(impl_->stmt);
	RunResult r;
#if SQLITE_VERSION_NUMBER >= 3037000
	r.changes = static_cast<std::int64_t>(sqlite3_changes64(db));
#else
	r.changes = static_cast<std::int64_t>(sqlite3_changes(db));
#endif
	r.last_insert_rowid = static_cast<std::int64_t>(sqlite3_last_insert_rowid(db));
	return r;
}

int Statement::column_count() const {
	live(impl_);
	return sqlite3_column_count(impl_->stmt);
}

std::string Statement::column_name(int index) const {
	sqlite3* db = live(impl_);
	if (index < 0 || index >= sqlite3_column_count(impl_->stmt)) throw Error(SQLITE_RANGE, "lapilli::sqlite: column index " + std::to_string(index) + " out of range");
	const char* name = sqlite3_column_name(impl_->stmt, index);
	if (!name) detail::throw_error(db, SQLITE_NOMEM, "column_name");
	return name;
}

namespace {

//行の上にいて、列が範囲内であることを確かめる (範囲外や行の無い状態で読むと SQLite は不定の値を返す)
sqlite3* readable(const std::unique_ptr<detail::StatementImpl>& impl, int index) {
	sqlite3* db = live(impl);
	if (!impl->has_row) detail::throw_misuse("column read without a current row (step() must return true first)");
	if (index < 0 || index >= sqlite3_column_count(impl->stmt)) throw Error(SQLITE_RANGE, "lapilli::sqlite: column index " + std::to_string(index) + " out of range");
	return db;
}

}

bool Statement::column_is_null(int index) const {
	readable(impl_, index);
	return sqlite3_column_type(impl_->stmt, index) == SQLITE_NULL;
}

std::int64_t Statement::column_int64(int index) const {
	readable(impl_, index);
	return static_cast<std::int64_t>(sqlite3_column_int64(impl_->stmt, index));
}

double Statement::column_double(int index) const {
	readable(impl_, index);
	return sqlite3_column_double(impl_->stmt, index);
}

std::string Statement::column_text(int index) const {
	sqlite3* db = readable(impl_, index);
	const bool is_null = sqlite3_column_type(impl_->stmt, index) == SQLITE_NULL;
	//text を先に取り、その後で bytes を取る (逆順だと変換前の長さを読むことがある)
	const unsigned char* p = sqlite3_column_text(impl_->stmt, index);
	const int n = sqlite3_column_bytes(impl_->stmt, index);
	if (!p) {
		if (is_null) return std::string();
		detail::throw_error(db, SQLITE_NOMEM, "column_text");
	}
	return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}

std::vector<std::uint8_t> Statement::column_blob(int index) const {
	sqlite3* db = readable(impl_, index);
	const void* p = sqlite3_column_blob(impl_->stmt, index);
	const int n = sqlite3_column_bytes(impl_->stmt, index);
	if (!p) {
		//NULL と長さ 0 の blob はどちらも nullptr。メモリ不足だけは区別して投げる
		if (sqlite3_errcode(db) == SQLITE_NOMEM) detail::throw_error(db, SQLITE_NOMEM, "column_blob");
		return {};
	}
	const auto* bytes = static_cast<const std::uint8_t*>(p);
	return std::vector<std::uint8_t>(bytes, bytes + n);
}

}
