// Child processes without a shell. The program and its arguments are an argument vector
// (posix_spawn / CreateProcessW with each argument quoted for the C runtime's parser; never
// `sh -c` or `cmd /c`, and a .bat / .cmd program is refused on Windows because CreateProcess would
// hand it to cmd.exe).
//
// Every run has a deadline and a cap on each output pipe (charged to a Lorica budget). The child
// runs in its own process group (POSIX) / Job Object (Windows), so a timeout, a cancel or an
// output overflow kills the whole tree, grandchildren included. Writing the stdin data never
// raises SIGPIPE (the signal is blocked on the writing thread and a pending one is consumed);
// a child that exits without reading its stdin is not an error.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lapilli::process {

using EnvList = std::vector<std::pair<std::string, std::string>>;

enum class EnvMode {
    /** The parent's environment, minus `env_remove`, with `env` added / replacing (the default). */
    inherit_overlay,
    /** Exactly `env`. */
    replace,
    /** No variables at all. */
    empty,
};

struct Spec {
    /** argv[0] is the program: a path, or a name looked up on PATH (the child's PATH when the env sets one). */
    std::vector<std::string> argv;
    std::optional<std::filesystem::path> cwd;
    EnvMode env_mode = EnvMode::inherit_overlay;
    EnvList env;
    /** Names removed from the inherited environment (inherit_overlay only; case-insensitive on Windows). */
    std::vector<std::string> env_remove;
    /** Written to stdin, then stdin is closed (unset: stdin is empty). */
    std::optional<std::string> stdin_data;
    /** Whole-run deadline, including the time grandchildren keep the output pipes open. Zero or less: none. */
    std::chrono::milliseconds timeout{std::chrono::minutes(2)};
    std::size_t max_stdout = 32u << 20;
    std::size_t max_stderr = 32u << 20;
    /** Keep stdout (false: it is read and discarded, uncounted). */
    bool capture_stdout = true;
    /** stderr keeps only its last max_stderr bytes instead of failing the run when it is longer. */
    bool stderr_tail_only = false;
    /** Windows: CREATE_NO_WINDOW (Node's windowsHide). Ignored elsewhere. */
    bool hide_window = true;
};

enum class SpawnErrorKind {
    /** argv is empty or an argument / variable contains a NUL. */
    invalid_argument,
    /** The program does not exist (ENOENT, also when not found on PATH). */
    not_found,
    /** The program exists but cannot be executed (EACCES, ENOEXEC, ...). */
    not_executable,
    /** A .bat / .cmd program on Windows (it would run through cmd.exe). */
    refused_script,
    /** Any other OS failure (pipes, fork, job object). */
    os_error,
};

struct SpawnError {
    SpawnErrorKind kind = SpawnErrorKind::os_error;
    /** "ENOENT", "EACCES", ... (Windows: the closest errno name, or "EUNKNOWN"). */
    std::string errno_name;
    std::string message;
};

struct Result {
    /** Set when the child never started; every other field is then default. */
    std::optional<SpawnError> spawn_error;
    /** Exit code of a normal exit; -1 when the child was killed or ended by a signal. */
    int exit_code = -1;
    /** POSIX: the signal that ended the child (SIGKILL when this library killed it), 0 otherwise. */
    int signal = 0;
    bool timed_out = false;
    bool cancelled = false;
    /** stdout (or stderr without stderr_tail_only) passed its cap; the tree was killed. */
    bool output_overflow = false;
    std::string out;
    std::string err;
    /** stderr_tail_only: bytes dropped from the front of `err`. */
    std::size_t err_dropped = 0;
    std::chrono::milliseconds duration{0};

    /** Started, exited 0, and none of timeout / cancel / overflow. */
    [[nodiscard]] bool ok() const noexcept {
        return !spawn_error && exit_code == 0 && !timed_out && !cancelled && !output_overflow;
    }
};

/**
 * Runs the child to its end and returns what happened; never throws for the child's own failures
 * (spawn failures are Result::spawn_error). `cancel` (optional) is polled; once it is true the
 * tree is killed and Result::cancelled is set.
 */
Result run(const Spec& spec, const std::atomic<bool>* cancel = nullptr);

/** SIGKILL -> "SIGKILL", unknown -> "SIG<n>". Empty for 0. */
std::string signal_name(int signal);

/**
 * The program `name` would run as: a name with a directory part is checked as it is; a bare name
 * is searched on `path_list` (unset: the PATH of this process). Windows: PATHEXT order inside each
 * directory (a name that has an extension is tried as is). POSIX: a regular file with execute
 * permission. The current directory is never searched implicitly.
 */
std::optional<std::string> find_executable(const std::string& name, const std::optional<std::string>& path_list = std::nullopt);

/** find_executable's search with injected file checks (for tests and cross-platform callers). */
struct SearchRules {
    bool windows = false;
    std::string path_list;
    /** Windows PATHEXT (empty = ".COM;.EXE;.BAT;.CMD"). */
    std::string path_ext;
    std::function<bool(const std::string&)> is_executable;
};
std::optional<std::string> find_executable_with(const std::string& name, const SearchRules& rules);

/** True for a name ending in .bat / .cmd (any case): never handed to CreateProcess. */
bool is_shell_script_name(std::string_view program) noexcept;

/** One argument quoted for CommandLineToArgvW / the MSVC C runtime. */
std::string quote_windows_argument(const std::string& argument);
/** The whole command line (each argument quoted, separated by spaces). */
std::string windows_command_line(const std::vector<std::string>& argv);

/** The environment of this process, in its own order. */
EnvList current_environment();
/** The value of `name` in `env` (case-insensitive on Windows). */
std::optional<std::string> env_get(const EnvList& env, std::string_view name);

} // namespace lapilli::process
