#lapilli::sqlite — SQLite の薄いラッパー
#  add_subdirectory(<lapilli>/cpp) からも、add_subdirectory(<lapilli>/cpp/sqlite) からも読まれる
cmake_minimum_required(VERSION 3.16)

#cpp/ から読まれたときは上で決まっている。直接読まれたときはテストを作らない (取り込む側のビルドを増やさない)
option(LAPILLI_BUILD_TESTS "Build the Lapilli C++ tests" OFF)

#Lorica は任意 (LAPILLI_LORICA_DIR)。パーツ単位で取り込まれても効くよう、ここで読む
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/LapilliLorica.cmake)

#SQLite はシステムのものを使う。amalgamation を取得・同梱しない
find_package(SQLite3 REQUIRED)

add_library(lapilli_sqlite STATIC
	src/error.cpp
	src/identifier.cpp
	src/database.cpp
	src/statement.cpp
	src/transaction.cpp
)
add_library(lapilli::sqlite ALIAS lapilli_sqlite)
target_include_directories(lapilli_sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(lapilli_sqlite PUBLIC cxx_std_17)
set_target_properties(lapilli_sqlite PROPERTIES CXX_EXTENSIONS OFF)
#公開ヘッダは sqlite3.h を読まないので PRIVATE (静的ライブラリなのでリンクだけは利用側へ伝わる)
target_link_libraries(lapilli_sqlite PRIVATE SQLite::SQLite3)

if(MSVC)
	target_compile_options(lapilli_sqlite PRIVATE /W4 /utf-8)
else()
	target_compile_options(lapilli_sqlite PRIVATE -Wall -Wextra -Wpedantic)
endif()
lapilli_harden(lapilli_sqlite)

if(LAPILLI_BUILD_TESTS)
	enable_testing()
	find_package(Threads REQUIRED)
	add_executable(lapilli_sqlite_tests
		tests/main.cpp
		tests/database_test.cpp
		tests/statement_test.cpp
		tests/transaction_test.cpp
		tests/concurrency_test.cpp
		tests/gps_locations_test.cpp
	)
	#テストは結果コードを sqlite3.h の定数と突き合わせるので SQLite を直接見る
	target_link_libraries(lapilli_sqlite_tests PRIVATE lapilli_sqlite SQLite::SQLite3 Threads::Threads)
	set_target_properties(lapilli_sqlite_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(MSVC)
		target_compile_options(lapilli_sqlite_tests PRIVATE /W4 /utf-8)
	else()
		target_compile_options(lapilli_sqlite_tests PRIVATE -Wall -Wextra -Wpedantic)
	endif()
	lapilli_harden(lapilli_sqlite_tests)
	add_test(NAME lapilli_sqlite_tests COMMAND lapilli_sqlite_tests)
endif()
