// A Windows host without the symlink privilege can still run the ordinary file tests.
#include "lapilli/fs.hpp"

#include <chrono>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace lapilli::fs;

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = stdfs::temp_directory_path() / ("lapilli-symlink-" + std::to_string(stamp));
    if (!stdfs::create_directory(dir)) return 1;
    struct Cleanup {
        stdfs::path dir;
        ~Cleanup() {
            std::error_code ignored;
            stdfs::remove_all(dir, ignored);
        }
    } cleanup{dir};
    write_file(dir / "real" / "file", "data");
    std::error_code error;
    stdfs::create_directory_symlink(dir / "real", dir / "dir-link", error);
    if (!error) stdfs::create_symlink(dir / "real" / "file", dir / "file-link", error);
    if (error) {
        std::cerr << "create symlink: " << error.message() << '\n';
#ifdef _WIN32
        if (error == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category())) return 77;
#endif
        return 1;
    }
    ListOptions options;
    options.include_directories = true;
    return list_files_recursive(dir, options) == std::vector<std::string>{"real/", "real/file"} ? 0 : 1;
}
