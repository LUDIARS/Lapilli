#include "lapilli/fs.hpp"

#include "lapilli/text.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lapilli::fs {

std::string to_posix(const stdfs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

std::string to_utf8(const stdfs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

stdfs::path from_utf8(std::string_view text) {
    const std::u8string bytes(text.begin(), text.end());
    return stdfs::path(bytes);
}

namespace {

bool is_missing(const stdfs::path& path) {
    std::error_code error;
    const auto status = stdfs::status(path, error);
    return status.type() == stdfs::file_type::not_found ||
           error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
}

[[noreturn]] void fail_write(const stdfs::path& path, const std::string& reason) {
    throw FsError(ReadStatus::io_error, "cannot write " + to_posix(path) + ": " + reason);
}

void ensure_parent(const stdfs::path& path, bool create_parents) {
    if (!create_parents || !path.has_parent_path()) return;
    std::error_code error;
    stdfs::create_directories(path.parent_path(), error);
    if (error) fail_write(path, error.message());
}

} // namespace

ReadResult read_file(const stdfs::path& path, std::size_t max_bytes) {
    ReadResult result;
    std::error_code type_error;
    if (stdfs::is_directory(path, type_error)) {
        result.error = to_posix(path) + ": is a directory";
        return result;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.status = is_missing(path) ? ReadStatus::missing : ReadStatus::io_error;
        result.error = (result.status == ReadStatus::missing ? "ENOENT: no such file or directory, open '" : "cannot open '") + to_posix(path) + "'";
        return result;
    }
    // The size is a hint only (the file may grow or be a pipe); the budget decides.
    std::error_code size_error;
    const auto size = stdfs::file_size(path, size_error);
    if (!size_error && size > max_bytes) {
        result.status = ReadStatus::too_large;
        result.error = to_posix(path) + ": file is larger than " + std::to_string(max_bytes) + " bytes";
        return result;
    }
    lorica::input_budget budget(byte_limits(max_bytes));
    std::string bytes;
    if (!size_error) bytes.reserve(static_cast<std::size_t>(size));
    std::array<char, 65536> chunk{};
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) break;
        if (!budget.consume(got, 0, 0)) {
            result.status = ReadStatus::too_large;
            result.error = to_posix(path) + ": file is larger than " + std::to_string(max_bytes) + " bytes";
            return result;
        }
        bytes.append(chunk.data(), got);
    }
    if (in.bad()) {
        result.error = "cannot read '" + to_posix(path) + "'";
        return result;
    }
    result.status = ReadStatus::ok;
    result.bytes = std::move(bytes);
    return result;
}

std::string read_file_or_throw(const stdfs::path& path, std::size_t max_bytes) {
    auto result = read_file(path, max_bytes);
    if (result.status != ReadStatus::ok) throw FsError(result.status, result.error);
    return std::move(result.bytes);
}

void write_file(const stdfs::path& path, std::string_view bytes, bool create_parents) {
    ensure_parent(path, create_parents);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) fail_write(path, "cannot open");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) fail_write(path, "write failed");
}

void append_file(const stdfs::path& path, std::string_view bytes, bool create_parents) {
    ensure_parent(path, create_parents);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) fail_write(path, "cannot open");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) fail_write(path, "write failed");
}

namespace {

std::atomic<unsigned long long> g_temp_counter{0};

#ifdef _WIN32
unsigned long process_id() { return static_cast<unsigned long>(GetCurrentProcessId()); }
#else
unsigned long process_id() { return static_cast<unsigned long>(::getpid()); }
#endif

/** Exclusive-create the temp file, write, flush to disk. False when the name already exists. */
bool write_new(const stdfs::path& temp, std::string_view bytes) {
#ifdef _WIN32
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS) return false;
        fail_write(temp, "cannot create (Windows error " + std::to_string(GetLastError()) + ")");
    }
    std::size_t written = 0;
    bool ok = true;
    while (ok && written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - written, 1u << 30));
        DWORD n = 0;
        ok = WriteFile(file, bytes.data() + written, chunk, &n, nullptr) != 0;
        written += n;
    }
    ok = ok && FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp.c_str());
        fail_write(temp, "write failed");
    }
    return true;
#else
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return false;
        fail_write(temp, std::strerror(errno));
    }
    std::size_t written = 0;
    bool ok = true;
    while (ok && written < bytes.size()) {
        const auto n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) continue;
        ok = n > 0;
        if (ok) written += static_cast<std::size_t>(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    if (!ok) {
        ::unlink(temp.c_str());
        fail_write(temp, "write failed");
    }
    return true;
#endif
}

} // namespace

void write_file_atomic(const stdfs::path& path, std::string_view bytes, bool create_parents) {
    ensure_parent(path, create_parents);
    const stdfs::path directory = path.has_parent_path() ? path.parent_path() : stdfs::path(".");
    stdfs::path temp;
    for (int attempt = 0;; ++attempt) {
        if (attempt >= 100) fail_write(path, "no free temporary name");
        temp = directory / (path.filename().native() + from_utf8(".tmp-" + std::to_string(process_id()) + "-" +
                                                                 std::to_string(g_temp_counter.fetch_add(1))).native());
        if (write_new(temp, bytes)) break;
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temp.c_str());
        fail_write(path, "rename failed (Windows error " + std::to_string(code) + ")");
    }
#else
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        const int code = errno;
        ::unlink(temp.c_str());
        fail_write(path, std::string("rename failed: ") + std::strerror(code));
    }
    // Make the rename itself durable.
    const int dir = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
#endif
}

std::vector<std::string> list_files_recursive(const stdfs::path& root, const ListOptions& options) {
    std::vector<std::string> files;
    std::error_code error;
    stdfs::recursive_directory_iterator it(root, stdfs::directory_options::none, error);
    if (error) throw FsError(is_missing(root) ? ReadStatus::missing : ReadStatus::io_error, "cannot list " + to_posix(root) + ": " + error.message());
    lorica::input_budget budget({unlimited, options.max_entries, unlimited, 0});
    const stdfs::recursive_directory_iterator end;
    const auto fail = [&] { throw FsError(ReadStatus::io_error, "cannot list " + to_posix(root) + ": " + error.message()); };
    while (it != end) {
        if (!budget.consume(0, 1, 0)) throw BudgetExceeded(to_posix(root) + ": more than " + std::to_string(options.max_entries) + " entries");
        const auto& entry = *it;
        std::error_code kind_error;
        if (entry.is_symlink(kind_error)) {
            // never followed (directory_options::none) nor listed
        } else if (entry.is_regular_file(kind_error)) {
            files.push_back(to_posix(entry.path().lexically_relative(root)));
        } else if (options.include_directories && entry.is_directory(kind_error)) {
            files.push_back(to_posix(entry.path().lexically_relative(root)) + "/");
        }
        // increment(error) turns the iterator into `end` when a step fails (e.g. a subdirectory
        // it cannot enter), so the error is checked after every step, before the loop condition.
        it.increment(error);
        if (error) fail();
    }
    std::sort(files.begin(), files.end(), text::JsLess{});
    return files;
}

} // namespace lapilli::fs
