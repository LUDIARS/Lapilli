#Lorica (社内のハードニング) を Lapilli のターゲットに当てる。各パーツの CMakeLists.txt が読む
#  cpp/ から読まれても cpp/<part> から直接読まれても同じに効かせるため、トップではなくパーツ側で読む
#  パーツを複数取り込んでも、Lorica の読み込みと LAPILLI_LORICA_DIR の判定は 1 回だけにする
include_guard(GLOBAL)

#Lorica は任意。パスを渡したときだけ使う
set(LAPILLI_LORICA_DIR "" CACHE PATH "Path to a Lorica checkout; when set, lorica_harden_target is applied to the Lapilli C++ targets")

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
			#最初に読んだパーツのディレクトリに依らないよう、ビルドツリーの決まった場所に置く
			add_subdirectory(${LAPILLI_LORICA_DIR} ${CMAKE_BINARY_DIR}/_lapilli_lorica)
		endif()
	endif()
endif()

#LAPILLI_LORICA_DIR を渡していなければ何もしない (渡していないときの挙動を変えない)
function(lapilli_harden)
	if(NOT LAPILLI_LORICA_DIR)
		return()
	endif()
	foreach(target IN LISTS ARGN)
		lorica_harden_target(${target})
	endforeach()
endfunction()
