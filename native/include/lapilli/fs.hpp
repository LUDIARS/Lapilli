// File system access with bounded reads (Lorica budget on the bytes before they are stored),
// plain / atomic writes and a recursive listing that never follows symbolic links. Paths are
// std::filesystem paths; text is UTF-8 (from_utf8 / to_posix convert).
#pragma once

#include "lapilli/budget.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::fs {

namespace stdfs = std::filesystem;

enum class ReadStatus { ok, missing, too_large, io_error };

struct ReadResult {
    ReadStatus status = ReadStatus::io_error;
    /** The bytes (ok only). */
    std::string bytes;
    /** What went wrong (not ok). Never contains file contents. */
    std::string error;
};

class FsError : public std::runtime_error {
public:
    FsError(ReadStatus status, const std::string& what) : std::runtime_error(what), status_(status) {}
    [[nodiscard]] ReadStatus status() const noexcept { return status_; }

private:
    ReadStatus status_;
};

/** The whole file, refusing more than max_bytes (exactly max_bytes is read). missing = ENOENT / ENOTDIR. */
ReadResult read_file(const stdfs::path& path, std::size_t max_bytes);
/** read_file that throws FsError for every status but ok. */
std::string read_file_or_throw(const stdfs::path& path, std::size_t max_bytes);

/** Writes the bytes (truncating), creating parent directories when asked. Throws FsError(io_error). */
void write_file(const stdfs::path& path, std::string_view bytes, bool create_parents = true);
void append_file(const stdfs::path& path, std::string_view bytes, bool create_parents = true);
/**
 * Writes to a new temporary file in the same directory (exclusive create), flushes it to disk and
 * renames it over `path`: readers see the old or the new file, never a part. Throws FsError.
 */
void write_file_atomic(const stdfs::path& path, std::string_view bytes, bool create_parents = true);

struct ListOptions {
    /** Every entry seen (directories and others too) is charged; more throws BudgetExceeded. */
    std::size_t max_entries = 200000;
    /** Also list directories (with a trailing '/'). */
    bool include_directories = false;
};
/**
 * Regular files under root (recursive), as '/'-separated paths relative to root, sorted in
 * JavaScript string order. Symbolic links are neither followed nor listed (Node's
 * readdir({recursive}) + Dirent#isFile()). Throws FsError when root cannot be read.
 */
std::vector<std::string> list_files_recursive(const stdfs::path& root, const ListOptions& options = {});

/** Path as UTF-8 with '/' separators. */
std::string to_posix(const stdfs::path& path);
/** Path as UTF-8 (native separators). */
std::string to_utf8(const stdfs::path& path);
/** UTF-8 text -> path. */
stdfs::path from_utf8(std::string_view text);

} // namespace lapilli::fs
