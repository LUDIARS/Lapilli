# Dependencies of the Lapilli native library. The allow-list is closed: the C++20 standard
# library, OS APIs, Boost 1.87.0 headers (Asio / Beast), nlohmann_json 3.11.3, OpenSSL 3 and
# Lorica (cab4f12). Nothing else may be added here.
#
# Every dependency is reused when the consumer already has it:
#   - Lorica:        an existing Lorica::boundary target (and lorica_harden_target()), or
#                    LORICA_SOURCE_DIR (a local checkout), or the pinned commit.
#   - nlohmann_json: an existing nlohmann_json::nlohmann_json target, or the pinned release.
#   - Boost headers: LAPILLI_BOOST_TARGET (an existing INTERFACE target that carries the Boost
#                    include directory, e.g. a consumer's own), or the pinned release.
# FETCHCONTENT_SOURCE_DIR_LORICA / _JSON / _BOOST point a declaration at a local copy (no
# download), as FetchContent always allows.
include_guard(GLOBAL)
include(FetchContent)

find_package(Threads REQUIRED)

# --- Lorica ------------------------------------------------------------------------------------
if(NOT TARGET Lorica::boundary)
  set(LORICA_SOURCE_DIR "" CACHE PATH "Local Lorica checkout (empty = fetch the pinned commit)")
  set(LORICA_BUILD_PROTECTION OFF CACHE BOOL "" FORCE)
  set(LORICA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  if(LORICA_SOURCE_DIR)
    add_subdirectory("${LORICA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/lorica-build")
  else()
    FetchContent_Declare(lorica
      GIT_REPOSITORY https://github.com/LUDIARS/Lorica.git
      GIT_TAG cab4f12086e30484f249d1b4aa479bf7cdec13a9)
    FetchContent_MakeAvailable(lorica)
  endif()
endif()
if(NOT COMMAND lorica_harden_target)
  message(FATAL_ERROR "Lapilli: Lorica::boundary exists but lorica_harden_target() is not defined; include LoricaHardening.cmake first")
endif()

# --- nlohmann_json -----------------------------------------------------------------------------
if(NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(json)
endif()

# --- OpenSSL 3 (crypto and net parts) ----------------------------------------------------------
if(LAPILLI_NATIVE_CRYPTO OR LAPILLI_NATIVE_NET)
  if(NOT TARGET OpenSSL::Crypto)
    find_package(OpenSSL 3 REQUIRED)
  endif()
endif()

# --- Boost headers (net part) ------------------------------------------------------------------
if(LAPILLI_NATIVE_NET AND NOT TARGET lapilli_boost)
  set(LAPILLI_BOOST_TARGET "" CACHE STRING "Existing INTERFACE target carrying the Boost 1.87 headers (empty = fetch them)")
  add_library(lapilli_boost INTERFACE)
  if(LAPILLI_BOOST_TARGET)
    if(NOT TARGET ${LAPILLI_BOOST_TARGET})
      message(FATAL_ERROR "Lapilli: LAPILLI_BOOST_TARGET=${LAPILLI_BOOST_TARGET} is not a target")
    endif()
    target_link_libraries(lapilli_boost INTERFACE ${LAPILLI_BOOST_TARGET})
  else()
    # Headers only: SOURCE_SUBDIR names a directory that does not exist, so Boost's own build is
    # never configured. A consumer that declared `boost` first keeps its declaration.
    FetchContent_Declare(boost
      URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-b2-nodocs.zip
      URL_HASH SHA256=40efbb1f38b8cf186fd7bb12809f561ae1c233bae0f75146862e9bfce77d0d42
      SOURCE_SUBDIR lapilli-header-only)
    FetchContent_MakeAvailable(boost)
    target_include_directories(lapilli_boost SYSTEM INTERFACE "${boost_SOURCE_DIR}")
  endif()
  target_compile_definitions(lapilli_boost INTERFACE BOOST_ERROR_CODE_HEADER_ONLY BOOST_ASIO_NO_DEPRECATED)
  target_link_libraries(lapilli_boost INTERFACE Threads::Threads)
  if(WIN32)
    target_link_libraries(lapilli_boost INTERFACE ws2_32 mswsock crypt32)
  endif()
endif()
