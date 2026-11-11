#pragma once
//SQLite の薄いラッパー。sqlite3.h は見せない (呼び出し側が SQLite の型や定数に直結しないように)
//
//  - 値は必ず bind で渡す。SQL 文字列へ値を埋め込む補助は作らない (注入の入口を作らないため)
//  - すべての sqlite3_* の戻り値を確かめ、失敗は Error で投げる
//  - 1 つの Database は 1 つのスレッドで使う。このラッパーはロックを持たない
//    (スレッドごとに Database を開く。WAL なら読み手と書き手が並べる)
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lapilli::sqlite {

//SQLite の結果コード (一次コード)。値は SQLite が公開している安定した番号 (src/error.cpp で sqlite3.h と照合する)
namespace code {
constexpr int ok = 0;
constexpr int error = 1;
constexpr int busy = 5;
constexpr int locked = 6;
constexpr int readonly = 8;
constexpr int cantopen = 14;
constexpr int constraint = 19;
constexpr int mismatch = 20;
constexpr int misuse = 21;
constexpr int range = 25;
}

//SQLite の失敗。code() は一次コード (SQLITE_CONSTRAINT など)、extended_code() は拡張コード (SQLITE_CONSTRAINT_UNIQUE など)
class Error : public std::runtime_error {
public:
	Error(int extended_code, const std::string& message);
	int code() const noexcept { return extended_code_ & 0xff; }
	int extended_code() const noexcept { return extended_code_; }

private:
	int extended_code_;
};

//blob を bind するときの参照。bind の間だけ有効であればよい (SQLite が複製する)
struct BlobView {
	const void* data = nullptr;
	std::size_t size = 0;
	BlobView() = default;
	BlobView(const void* d, std::size_t n) : data(d), size(n) {}
	BlobView(const std::vector<std::uint8_t>& v) : data(v.data()), size(v.size()) {}
};

struct OpenOptions {
	bool read_only = false;
	bool create = false;	//false なら無いファイルは開かずに失敗する (別プロセスの DB を開くつもりで空の DB を作らないように)
	int busy_timeout_ms = 5000;	//ロック待ちの上限。過ぎたら SQLITE_BUSY の Error
	bool wal = false;	//true なら journal_mode=WAL にする (できなければ失敗)
	bool foreign_keys = true;
};

enum class TransactionMode { deferred, immediate, exclusive };

//run() の結果
struct RunResult {
	std::int64_t changes = 0;
	std::int64_t last_insert_rowid = 0;
};

class Database;

namespace detail {
struct Connection;
struct StatementImpl;
template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};
template <class T>
struct dependent_false : std::false_type {};
}

//準備済みの文。Database より長く生かさない (生かした場合、使うと Error を投げる。破棄は安全)
class Statement {
public:
	~Statement();
	Statement(Statement&&) noexcept;
	Statement& operator=(Statement&&) noexcept;
	Statement(const Statement&) = delete;
	Statement& operator=(const Statement&) = delete;

	//bind (index は 1 始まり)。文字列と blob は SQLite が複製するので、呼び出し後に元を捨ててよい
	Statement& bind_int64(int index, std::int64_t value);
	Statement& bind_double(int index, double value);
	Statement& bind_text(int index, std::string_view value);
	Statement& bind_blob(int index, BlobView value);
	Statement& bind_null(int index);

	//型で振り分ける bind。整数 / 浮動小数 / 文字列 / blob / nullptr / std::nullopt / std::optional<それら>
	//  bool は受けない (ポインタの取り違えなどで意図せず整数になるのを防ぐ。整数として明示的に渡す)
	template <class T>
	Statement& bind(int index, const T& value) {
		using V = std::decay_t<T>;
		if constexpr (std::is_same_v<V, std::nullptr_t> || std::is_same_v<V, std::nullopt_t>) {
			return bind_null(index);
		} else if constexpr (detail::is_optional<V>::value) {
			if (value) return bind(index, *value);
			return bind_null(index);
		} else if constexpr (std::is_same_v<V, bool>) {
			static_assert(detail::dependent_false<T>::value, "bool は整数として明示的に bind する");
			return *this;
		} else if constexpr (std::is_integral_v<V>) {
			if constexpr (std::is_unsigned_v<V> && sizeof(V) >= sizeof(std::int64_t)) {
				if (value > static_cast<V>(std::numeric_limits<std::int64_t>::max())) throw Error(code::mismatch, "lapilli::sqlite: unsigned value does not fit in int64");
			}
			return bind_int64(index, static_cast<std::int64_t>(value));
		} else if constexpr (std::is_floating_point_v<V>) {
			return bind_double(index, static_cast<double>(value));
		} else if constexpr (std::is_same_v<V, BlobView> || std::is_same_v<V, std::vector<std::uint8_t>>) {
			return bind_blob(index, BlobView(value));
		} else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
			return bind_text(index, std::string_view(value));
		} else {
			static_assert(detail::dependent_false<T>::value, "bind できない型");
			return *this;
		}
	}

	//引数を 1 番から順に bind する。数が文のパラメータ数と違えば Error (渡し忘れを NULL にしないため)
	template <class... Args>
	Statement& bind_all(const Args&... args) {
		check_parameter_count(static_cast<int>(sizeof...(Args)));
		int index = 0;
		(void)index;
		(bind(++index, args), ...);
		return *this;
	}

	//1 行進める。行があれば true、終わりなら false。失敗は Error (文は巻き戻してから投げる。bind は残る)
	bool step();
	//巻き戻して bind も消す
	void reset();

	//列 (index は 0 始まり)。行の上にいないとき・範囲外は Error
	//  NULL は 0 / 0.0 / 空として読める (SQLite の変換規則)。NULL を区別するなら column_is_null か column_optional
	int column_count() const;
	std::string column_name(int index) const;
	bool column_is_null(int index) const;
	std::int64_t column_int64(int index) const;
	double column_double(int index) const;
	std::string column_text(int index) const;	//UTF-8。途中の NUL も含めて返す
	std::vector<std::uint8_t> column_blob(int index) const;

	//NULL なら nullopt。T は std::int64_t / double / std::string / std::vector<std::uint8_t>
	template <class T>
	std::optional<T> column_optional(int index) const {
		if (column_is_null(index)) return std::nullopt;
		if constexpr (std::is_same_v<T, std::int64_t>) return column_int64(index);
		else if constexpr (std::is_same_v<T, double>) return column_double(index);
		else if constexpr (std::is_same_v<T, std::string>) return column_text(index);
		else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) return column_blob(index);
		else static_assert(detail::dependent_false<T>::value, "column_optional できない型");
	}

	//最後まで実行する (INSERT / UPDATE / DELETE など)。行が返る文は Error。終われば巻き戻す (bind は残る)
	RunResult run();

	//最初の行を read(const Statement&) で読む。行が無ければ nullopt。終われば巻き戻す
	//  (途中で止めた文は読み取りのスナップショットを握り続けるので、必ず戻す)
	template <class F>
	auto first(F&& read) -> std::optional<std::decay_t<std::invoke_result_t<F, const Statement&>>> {
		Rewind guard(*this);
		rewind();
		if (!step()) return std::nullopt;
		return std::forward<F>(read)(static_cast<const Statement&>(*this));
	}

	//すべての行で fn(const Statement&) を呼ぶ。終われば (例外でも) 巻き戻す
	template <class F>
	void for_each(F&& fn) {
		Rewind guard(*this);
		rewind();
		while (step()) fn(static_cast<const Statement&>(*this));
	}

private:
	friend class Database;
	explicit Statement(std::unique_ptr<detail::StatementImpl> impl);

	//bind を残して巻き戻す
	void rewind() noexcept;
	void check_parameter_count(int given) const;

	struct Rewind {
		Statement& s;
		explicit Rewind(Statement& st) : s(st) {}
		~Rewind() { s.rewind(); }
		Rewind(const Rewind&) = delete;
		Rewind& operator=(const Rewind&) = delete;
	};

	std::unique_ptr<detail::StatementImpl> impl_;
};

//接続 1 本。破棄で閉じる。複製はできず、移動はできる
class Database {
public:
	//path は UTF-8。URI としては解釈しない
	static Database open(const std::string& path, const OpenOptions& options = OpenOptions());

	~Database();
	Database(Database&&) noexcept;
	Database& operator=(Database&&) noexcept;
	Database(const Database&) = delete;
	Database& operator=(const Database&) = delete;

	//結果を返さない文 (複数可) をそのまま実行する。値を埋め込んだ SQL を渡さない (値は prepare + bind)
	void exec(const std::string& sql);
	//文を 1 つ準備する。2 つ以上の文が入っていれば Error (後ろの文が黙って無視されないように)
	Statement prepare(std::string_view sql);

	std::int64_t last_insert_rowid() const;
	std::int64_t changes() const;	//直前の文が変えた行数

	//テーブルの列名を定義順に返す。テーブルが無ければ空。
	//  テーブル名は bind できないので、識別子 ([A-Za-z_][A-Za-z0-9_]*) でなければ Error
	std::vector<std::string> table_columns(std::string_view table);

	//明示的に閉じる。以後この Database と、ここから作った Statement は使えない (Error)
	void close() noexcept;
	bool is_open() const noexcept { return static_cast<bool>(conn_); }

private:
	friend class Transaction;
	explicit Database(std::shared_ptr<detail::Connection> conn);
	detail::Connection& connection() const;

	std::shared_ptr<detail::Connection> conn_;
};

//BEGIN してから、commit() しないまま破棄されたら ROLLBACK する。破棄は例外を投げない
//  既定は IMMEDIATE: 書き込みのロックを最初に取る (DEFERRED は途中で書き込みに上げるときに待たずに BUSY になりうる)
//  接続を共有して持つので、Database を移動・close しても、このトランザクションの破棄までは接続が残る
class Transaction {
public:
	explicit Transaction(Database& db, TransactionMode mode = TransactionMode::immediate);
	~Transaction();
	Transaction(const Transaction&) = delete;
	Transaction& operator=(const Transaction&) = delete;

	//COMMIT。失敗 (BUSY など) は Error で、そのときトランザクションは生きたまま (破棄で戻る)
	void commit();
	//明示的に ROLLBACK する。失敗は Error
	void rollback();

private:
	std::shared_ptr<detail::Connection> conn_;
	bool active_ = true;
};

}
