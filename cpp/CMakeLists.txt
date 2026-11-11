#Lapilli の C++ パーツ一式。サービスは add_subdirectory(<lapilli>/cpp) か、パーツ単位で add_subdirectory(<lapilli>/cpp/<part>)
cmake_minimum_required(VERSION 3.16)
project(lapilli_cpp VERSION 0.1.0 LANGUAGES CXX)

#自分がトップのときだけテストを既定で作る (取り込む側のビルドにテストを混ぜない)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(lapilli_is_top_level ON)
else()
	set(lapilli_is_top_level OFF)
endif()
option(LAPILLI_BUILD_TESTS "Build the Lapilli C++ tests" ${lapilli_is_top_level})

#Lorica (社内のハードニング) は任意。パスを渡したときだけ使う
set(LAPILLI_LORICA_DIR "" CACHE PATH "Path to a Lorica checkout; when set, lorica_harden_target is applied to the Lapilli C++ targets")

if(LAPILLI_BUILD_TESTS)
	#ctest --test-dir <build> がトップから見つけられるように、ここで有効にする
	enable_testing()
endif()

add_subdirectory(sqlite)

if(LAPILLI_LORICA_DIR)
	#Lorica は CMake 3.24 以上を要る。使うときだけ求める (使わないなら 3.16 のまま)
	if(CMAKE_VERSION VERSION_LESS 3.24)
		message(FATAL_ERROR "LAPILLI_LORICA_DIR needs CMake 3.24 or newer (this is ${CMAKE_VERSION})")
	endif()
	#取り込む側が先に Lorica を読んでいれば、二重に add_subdirectory しない
	if(NOT COMMAND lorica_harden_target)
		if(TARGET lorica_boundary)
			include(${LAPILLI_LORICA_DIR}/cmake/LoricaHardening.cmake)
		else()
			add_subdirectory(${LAPILLI_LORICA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/_lorica)
		endif()
	endif()
	lorica_harden_target(lapilli_sqlite)
	if(TARGET lapilli_sqlite_tests)
		lorica_harden_target(lapilli_sqlite_tests)
	endif()
endif()
