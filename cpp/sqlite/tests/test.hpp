#pragma once
//依存を増やさないための最小のテスト枠 (Portus の tests/test.hpp と同じ形)
#include <filesystem>
#include <string>
#include <vector>

#include "lapilli/sqlite.hpp"

namespace testing {

struct Case {
	const char* name;
	void (*fn)();
};
std::vector<Case>& registry();
void report_failure(const char* file, int line, const std::string& what);

struct Registrar {
	Registrar(const char* name, void (*fn)()) { registry().push_back(Case{ name, fn }); }
};

//一時ディレクトリの中の、重ならない名前のファイル。破棄で本体と -wal / -shm / -journal を消す
//  ファイルは作らない (作るかどうかはテストが OpenOptions で決める)
class TempPath {
public:
	TempPath();
	~TempPath();
	TempPath(const TempPath&) = delete;
	TempPath& operator=(const TempPath&) = delete;
	const std::string& str() const { return path_; }

private:
	std::string path_;
};

//create=true で開く (テストの多くは新しい DB から始める)
lapilli::sqlite::Database open_new(const TempPath& path);

}

#define TEST(id, description) \
	static void test_##id(); \
	static testing::Registrar registrar_##id(description, test_##id); \
	static void test_##id()

#define CHECK(cond) \
	do { \
		if (!(cond)) testing::report_failure(__FILE__, __LINE__, #cond); \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		if (!((a) == (b))) testing::report_failure(__FILE__, __LINE__, #a " == " #b); \
	} while (0)

#define CHECK_THROWS(expr) \
	do { \
		bool threw = false; \
		try { \
			(void)(expr); \
		} catch (...) { \
			threw = true; \
		} \
		if (!threw) testing::report_failure(__FILE__, __LINE__, "expected exception: " #expr); \
	} while (0)

//lapilli::sqlite::Error が投げられ、その一次コードが expected であること
#define CHECK_SQLITE_ERROR(expr, expected) \
	do { \
		int got = -1; \
		try { \
			(void)(expr); \
		} catch (const lapilli::sqlite::Error& e) { \
			got = e.code(); \
		} \
		if (got != (expected)) testing::report_failure(__FILE__, __LINE__, "expected sqlite error " #expected " from " #expr ", got " + std::to_string(got)); \
	} while (0)
