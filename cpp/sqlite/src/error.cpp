#include "internal.hpp"

namespace lapilli::sqlite {

//公開ヘッダの番号は sqlite3.h を読まずに書いているので、ここで食い違いを止める
static_assert(code::ok == SQLITE_OK);
static_assert(code::error == SQLITE_ERROR);
static_assert(code::busy == SQLITE_BUSY);
static_assert(code::locked == SQLITE_LOCKED);
static_assert(code::readonly == SQLITE_READONLY);
static_assert(code::cantopen == SQLITE_CANTOPEN);
static_assert(code::constraint == SQLITE_CONSTRAINT);
static_assert(code::mismatch == SQLITE_MISMATCH);
static_assert(code::misuse == SQLITE_MISUSE);
static_assert(code::range == SQLITE_RANGE);

Error::Error(int extended_code, const std::string& message) : std::runtime_error(message), extended_code_(extended_code) {}

namespace detail {

std::string describe(sqlite3* db, int rc, std::string_view what) {
	std::string message = "lapilli::sqlite: ";
	message.append(what);
	message += ": ";
	//接続のメッセージは直前の失敗のもの。rc と食い違う場合 (別の呼び出しで上書き済み) は rc の説明を使う
	if (db && sqlite3_extended_errcode(db) == rc) message += sqlite3_errmsg(db);
	else message += sqlite3_errstr(rc);
	message += " (code ";
	message += std::to_string(rc);
	message += ")";
	return message;
}

void throw_error(sqlite3* db, int rc, std::string_view what) {
	throw Error(rc, describe(db, rc, what));
}

void throw_misuse(std::string_view what) {
	std::string message = "lapilli::sqlite: ";
	message.append(what);
	throw Error(SQLITE_MISUSE, message);
}

}

}
