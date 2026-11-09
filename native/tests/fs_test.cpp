// lapilli::fs: bounded reads (exact / one over / missing / directory), atomic writes, recursive
// listing without following symbolic links, with an entry budget.
#include "lapilli/fs.hpp"
#include "lapilli/test/check.hpp"

#include <chrono>

using namespace lapilli::fs;

namespace {
stdfs::path scratch(const std::string& name) {
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto dir = stdfs::temp_directory_path() / ("lapilli-fs-" + name + "-" + stamp);
    stdfs::create_directories(dir);
    return dir;
}
} // namespace

LAPILLI_TEST("boundary: a file of exactly max_bytes is read, one byte over is too_large; missing is missing") {
    const auto dir = scratch("read");
    write_file(dir / "a.txt", "12345678");
    const auto ok = read_file(dir / "a.txt", 8);
    LAPILLI_CHECK(ok.status == ReadStatus::ok);
    LAPILLI_CHECK_EQ(ok.bytes, std::string("12345678"));
    const auto over = read_file(dir / "a.txt", 7);
    LAPILLI_CHECK(over.status == ReadStatus::too_large);
    LAPILLI_CHECK(over.bytes.empty());
    LAPILLI_CHECK(read_file(dir / "none.txt", 8).status == ReadStatus::missing);
    LAPILLI_CHECK(read_file(dir / "none" / "x.txt", 8).status == ReadStatus::missing);
    LAPILLI_CHECK(read_file(dir, 8).status == ReadStatus::io_error);
    LAPILLI_CHECK_THROWS_AS(read_file_or_throw(dir / "none.txt", 8), FsError);
    LAPILLI_CHECK_EQ(read_file_or_throw(dir / "a.txt", 100), std::string("12345678"));
    write_file(dir / "empty", "");
    LAPILLI_CHECK(read_file(dir / "empty", 0).status == ReadStatus::ok);
    stdfs::remove_all(dir);
}

LAPILLI_TEST("writes create parents; append appends; atomic write replaces without leftovers") {
    const auto dir = scratch("write");
    write_file(dir / "deep" / "x" / "f.txt", "one");
    append_file(dir / "deep" / "x" / "f.txt", "+two");
    LAPILLI_CHECK_EQ(read_file_or_throw(dir / "deep" / "x" / "f.txt", 100), std::string("one+two"));
    write_file_atomic(dir / "state.json", "{\"v\":1}");
    write_file_atomic(dir / "state.json", "{\"v\":2}");
    LAPILLI_CHECK_EQ(read_file_or_throw(dir / "state.json", 100), std::string("{\"v\":2}"));
    LAPILLI_CHECK((list_files_recursive(dir) == std::vector<std::string>{"deep/x/f.txt", "state.json"}));
    stdfs::remove_all(dir);
}

LAPILLI_TEST("listing is recursive, JS-sorted, skips symbolic links and stops at its entry budget") {
    const auto dir = scratch("list");
    write_file(dir / "b.txt", "");
    write_file(dir / "a" / "z.txt", "");
    write_file(dir / "B.txt", "");
    std::error_code link_error;
    stdfs::create_directory_symlink(dir / "a", dir / "link", link_error);
    stdfs::create_symlink(dir / "b.txt", dir / "file-link", link_error);
    LAPILLI_CHECK((list_files_recursive(dir) == std::vector<std::string>{"B.txt", "a/z.txt", "b.txt"}));
    ListOptions with_dirs;
    with_dirs.include_directories = true;
    LAPILLI_CHECK((list_files_recursive(dir, with_dirs) == std::vector<std::string>{"B.txt", "a/", "a/z.txt", "b.txt"}));
    ListOptions tight;
    tight.max_entries = 3;
    LAPILLI_CHECK_THROWS_AS(list_files_recursive(dir, tight), lapilli::BudgetExceeded);
    LAPILLI_CHECK_THROWS_AS(list_files_recursive(dir / "missing"), FsError);
    LAPILLI_CHECK_EQ(to_posix(from_utf8("a/\xC3\xA9")), std::string("a/\xC3\xA9"));
    stdfs::remove_all(dir);
}
