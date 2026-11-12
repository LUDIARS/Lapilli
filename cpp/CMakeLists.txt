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

if(LAPILLI_BUILD_TESTS)
	#ctest --test-dir <build> がトップから見つけられるように、ここで有効にする
	enable_testing()
endif()

add_subdirectory(sqlite)

#Lorica (任意) は各パーツが cmake/LapilliLorica.cmake で自分のターゲットに当てる
