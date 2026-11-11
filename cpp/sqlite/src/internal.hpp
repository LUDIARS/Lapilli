#pragma once
//ライブラリ内だけで使う定義。sqlite3.h はここから先でだけ読む
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <sqlite3.h>

#include "lapilli/sqlite.hpp"

namespace lapilli::sqlite::detail {

//接続そのもの。Database が持ち、Statement は弱参照で生存を確かめる
//  閉じるのは sqlite3_close_v2: 片付け忘れの文が残っていても、文が finalize されるまで実体を残してくれる
//  (Database が先に消えても Statement の破棄が壊れたポインタを触らない)
struct Connection {
	sqlite3* db = nullptr;
	explicit Connection(sqlite3* handle) : db(handle) {}
	~Connection();
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;
};

//準備済みの文の実体。prepare (database.cpp) が作り、statement.cpp が使う
struct StatementImpl {
	std::weak_ptr<Connection> conn;
	sqlite3_stmt* stmt = nullptr;
	bool has_row = false;	//直前の step が行を返した (列を読んでよい)
	StatementImpl(std::weak_ptr<Connection> c, sqlite3_stmt* s) : conn(std::move(c)), stmt(s) {}
	~StatementImpl();
	StatementImpl(const StatementImpl&) = delete;
	StatementImpl& operator=(const StatementImpl&) = delete;
};

//失敗の説明。接続のメッセージは直前の失敗のものなので、後で別の呼び出しをする前に作る
std::string describe(sqlite3* db, int rc, std::string_view what);
//rc の失敗を、接続のエラーメッセージを添えて Error で投げる
[[noreturn]] void throw_error(sqlite3* db, int rc, std::string_view what);
//接続を伴わない失敗 (使い方の誤りなど)
[[noreturn]] void throw_misuse(std::string_view what);

//テーブル名などに使ってよい識別子か ([A-Za-z_][A-Za-z0-9_]*、128 文字まで)
bool is_identifier(std::string_view name);

}
