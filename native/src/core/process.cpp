#include "lapilli/process.hpp"

#include "lapilli/budget.hpp"
#include "lapilli/text.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <cwchar>
#include <thread>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace lapilli::process {

using Clock = std::chrono::steady_clock;

// --- platform-neutral helpers -------------------------------------------------------------------

std::string quote_windows_argument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos) return argument;
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : argument) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out += c;
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

std::string windows_command_line(const std::vector<std::string>& argv) {
    std::string line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) line += ' ';
        line += quote_windows_argument(argv[i]);
    }
    return line;
}

bool is_shell_script_name(std::string_view program) noexcept {
    if (program.size() < 4) return false;
    const auto ext = program.substr(program.size() - 4);
    return text::ascii_iequals(ext, ".bat") || text::ascii_iequals(ext, ".cmd");
}

std::optional<std::string> env_get(const EnvList& env, std::string_view name) {
    for (const auto& [key, value] : env) {
#ifdef _WIN32
        if (text::ascii_iequals(key, name)) return value;
#else
        if (key == name) return value;
#endif
    }
    return std::nullopt;
}

namespace {

bool same_name(std::string_view a, std::string_view b) {
#ifdef _WIN32
    return text::ascii_iequals(a, b);
#else
    return a == b;
#endif
}

bool is_sep(char c) { return c == '/' || c == '\\'; }

std::string windows_extname(const std::string& path) {
    std::size_t start = path.size();
    while (start > 0 && !is_sep(path[start - 1]) && path[start - 1] != ':') --start;
    const std::string name = path.substr(start);
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    return name.substr(dot);
}

EnvList effective_environment(const Spec& spec) {
    switch (spec.env_mode) {
    case EnvMode::empty:
        return {};
    case EnvMode::replace:
        return spec.env;
    case EnvMode::inherit_overlay:
        break;
    }
    EnvList out;
    for (auto& entry : current_environment()) {
        const bool removed = std::any_of(spec.env_remove.begin(), spec.env_remove.end(), [&](const std::string& n) { return same_name(n, entry.first); });
        const bool overlaid = std::any_of(spec.env.begin(), spec.env.end(), [&](const auto& kv) { return same_name(kv.first, entry.first); });
        if (!removed && !overlaid) out.push_back(std::move(entry));
    }
    for (const auto& kv : spec.env) out.push_back(kv);
    return out;
}

bool has_nul(std::string_view text) { return text.find('\0') != std::string_view::npos; }

std::optional<SpawnError> validate(const Spec& spec, const EnvList& env) {
    if (spec.argv.empty() || spec.argv[0].empty()) return SpawnError{SpawnErrorKind::invalid_argument, "EINVAL", "no program to run"};
    for (const auto& arg : spec.argv)
        if (has_nul(arg)) return SpawnError{SpawnErrorKind::invalid_argument, "EINVAL", "an argument contains a NUL byte"};
    for (const auto& [key, value] : env)
        if (key.empty() || has_nul(key) || has_nul(value) || key.find('=', 1) != std::string::npos)
            return SpawnError{SpawnErrorKind::invalid_argument, "EINVAL", "an environment variable is malformed"};
    return std::nullopt;
}

/** Collects one pipe's bytes under its cap (Lorica budget) or as a bounded tail. */
class Sink {
public:
    Sink(std::string* target, std::size_t cap, bool tail_only, std::size_t* dropped)
        : target_(target), budget_(byte_limits(cap)), cap_(cap), tail_only_(tail_only), dropped_(dropped) {}

    /** False when the bytes do not fit the cap (the caller kills the tree). */
    bool add(const char* data, std::size_t size) {
        if (target_ == nullptr) return true;
        if (tail_only_) {
            target_->append(data, size);
            // Trim in batches so the work stays linear; memory stays below 2 * cap + one read.
            if (target_->size() > cap_ * 2 + 65536) trim();
            return true;
        }
        if (!budget_.consume(size, 0, 0)) return false;
        target_->append(data, size);
        return true;
    }
    void finish() {
        if (tail_only_ && target_ != nullptr && target_->size() > cap_) trim();
    }

private:
    void trim() {
        const std::size_t drop = target_->size() - cap_;
        target_->erase(0, drop);
        if (dropped_ != nullptr) *dropped_ += drop;
    }
    std::string* target_;
    lorica::input_budget budget_;
    std::size_t cap_;
    bool tail_only_;
    std::size_t* dropped_;
};

} // namespace

std::optional<std::string> find_executable_with(const std::string& name, const SearchRules& rules) {
    if (name.empty() || !rules.is_executable) return std::nullopt;
    if (rules.windows) {
        std::vector<std::string> extensions;
        for (const auto& ext : text::split(rules.path_ext.empty() ? ".COM;.EXE;.BAT;.CMD" : rules.path_ext, ';'))
            if (!ext.empty()) extensions.push_back(text::ascii_lower(ext));
        std::vector<std::string> bases;
        const bool has_dir = std::any_of(name.begin(), name.end(), is_sep) || (name.size() >= 2 && name[1] == ':');
        if (has_dir) {
            bases.push_back(name);
        } else {
            for (const auto& dir : text::split(rules.path_list, ';')) {
                if (dir.empty()) continue;
                bases.push_back(is_sep(dir.back()) ? dir + name : dir + "\\" + name);
            }
        }
        for (const auto& base : bases) {
            if (!windows_extname(base).empty()) {
                if (rules.is_executable(base)) return base;
                continue;
            }
            for (const auto& ext : extensions)
                if (rules.is_executable(base + ext)) return base + ext;
        }
        return std::nullopt;
    }
    if (name.find('/') != std::string::npos) return rules.is_executable(name) ? std::optional<std::string>(name) : std::nullopt;
    for (const auto& dir : text::split(rules.path_list, ':')) {
        if (dir.empty()) continue;
        const std::string candidate = dir.back() == '/' ? dir + name : dir + "/" + name;
        if (rules.is_executable(candidate)) return candidate;
    }
    return std::nullopt;
}

namespace {

bool system_is_executable(const std::string& path) {
#ifdef _WIN32
    std::error_code error;
    const std::u8string bytes(path.begin(), path.end());
    return std::filesystem::is_regular_file(std::filesystem::path(bytes), error);
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && ::access(path.c_str(), X_OK) == 0;
#endif
}

SearchRules system_rules(const std::optional<std::string>& path_list, const std::optional<std::string>& path_ext) {
    SearchRules rules;
#ifdef _WIN32
    rules.windows = true;
#endif
    const EnvList env = current_environment();
    rules.path_list = path_list ? *path_list : env_get(env, "PATH").value_or("");
    rules.path_ext = path_ext ? *path_ext : env_get(env, "PATHEXT").value_or("");
    rules.is_executable = system_is_executable;
    return rules;
}

} // namespace

std::optional<std::string> find_executable(const std::string& name, const std::optional<std::string>& path_list) {
    return find_executable_with(name, system_rules(path_list, std::nullopt));
}

#ifndef _WIN32

// --- POSIX ----------------------------------------------------------------------------------------

std::string signal_name(int signal) {
    switch (signal) {
    case 0: return "";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    case SIGINT: return "SIGINT";
    case SIGHUP: return "SIGHUP";
    case SIGQUIT: return "SIGQUIT";
    case SIGABRT: return "SIGABRT";
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGPIPE: return "SIGPIPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGALRM: return "SIGALRM";
    case SIGUSR1: return "SIGUSR1";
    case SIGUSR2: return "SIGUSR2";
    default: return "SIG" + std::to_string(signal);
    }
}

EnvList current_environment() {
    EnvList out;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view text(*entry);
        const auto eq = text.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        out.emplace_back(std::string(text.substr(0, eq)), std::string(text.substr(eq + 1)));
    }
    return out;
}

namespace {

const char* errno_name(int error) {
    switch (error) {
    case ENOENT: return "ENOENT";
    case EACCES: return "EACCES";
    case ENOTDIR: return "ENOTDIR";
    case EINVAL: return "EINVAL";
    case ENOEXEC: return "ENOEXEC";
    case ENOMEM: return "ENOMEM";
    case EAGAIN: return "EAGAIN";
    case EMFILE: return "EMFILE";
    case ENFILE: return "ENFILE";
    case E2BIG: return "E2BIG";
    case EPERM: return "EPERM";
    case ELOOP: return "ELOOP";
    case ENAMETOOLONG: return "ENAMETOOLONG";
    case ETXTBSY: return "ETXTBSY";
    default: return "EUNKNOWN";
    }
}

SpawnError spawn_error_of(int error, const std::string& program) {
    SpawnErrorKind kind = SpawnErrorKind::os_error;
    if (error == ENOENT || error == ENOTDIR) kind = SpawnErrorKind::not_found;
    else if (error == EACCES || error == ENOEXEC || error == EPERM || error == ETXTBSY) kind = SpawnErrorKind::not_executable;
    return SpawnError{kind, errno_name(error), "spawn " + program + " " + errno_name(error) + " (" + std::strerror(error) + ")"};
}

struct Fd {
    int value = -1;
    Fd() = default;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }
    void reset() {
        if (value >= 0) ::close(value);
        value = -1;
    }
};

bool make_pipe(Fd& read_end, Fd& write_end) {
    int fds[2];
    if (::pipe(fds) != 0) return false;
    read_end.value = fds[0];
    write_end.value = fds[1];
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}

/** Blocks SIGPIPE on this thread for its lifetime; a SIGPIPE raised meanwhile is consumed. */
class SigpipeGuard {
public:
    SigpipeGuard() {
        sigset_t pipe_set;
        sigemptyset(&pipe_set);
        sigaddset(&pipe_set, SIGPIPE);
        sigset_t pending;
        sigemptyset(&pending);
        sigpending(&pending);
        was_pending_ = sigismember(&pending, SIGPIPE) == 1;
        blocked_ = pthread_sigmask(SIG_BLOCK, &pipe_set, &old_) == 0;
    }
    SigpipeGuard(const SigpipeGuard&) = delete;
    SigpipeGuard& operator=(const SigpipeGuard&) = delete;
    ~SigpipeGuard() {
        if (!blocked_) return;
        if (!was_pending_) {
            sigset_t pending;
            sigemptyset(&pending);
            sigpending(&pending);
            if (sigismember(&pending, SIGPIPE) == 1) {
                sigset_t pipe_set;
                sigemptyset(&pipe_set);
                sigaddset(&pipe_set, SIGPIPE);
                int consumed = 0;
                sigwait(&pipe_set, &consumed);
            }
        }
        pthread_sigmask(SIG_SETMASK, &old_, nullptr);
    }

private:
    sigset_t old_{};
    bool was_pending_ = false;
    bool blocked_ = false;
};

std::vector<std::string> environment_strings(const EnvList& env) {
    std::vector<std::string> out;
    out.reserve(env.size());
    for (const auto& [key, value] : env) out.push_back(key + "=" + value);
    return out;
}

} // namespace

Result run(const Spec& spec, const std::atomic<bool>* cancel) {
    const auto started = Clock::now();
    Result result;
    const EnvList env = effective_environment(spec);
    if (auto invalid = validate(spec, env)) {
        result.spawn_error = std::move(invalid);
        return result;
    }
    const std::string& name = spec.argv[0];
    std::string program = name;
    if (name.find('/') == std::string::npos) {
        // Look the bare name up on the child's PATH (as Node does), never the current directory.
        SearchRules rules;
        rules.path_list = env_get(env, "PATH").value_or(spec.env_mode == EnvMode::inherit_overlay ? "" : "/usr/bin:/bin");
        rules.is_executable = system_is_executable;
        auto found = find_executable_with(name, rules);
        if (!found) {
            result.spawn_error = spawn_error_of(ENOENT, name);
            return result;
        }
        program = *found;
    }

    Fd in_read, in_write, out_read, out_write, err_read, err_write;
    if (!make_pipe(in_read, in_write) || !make_pipe(out_read, out_write) || !make_pipe(err_read, err_write)) {
        result.spawn_error = spawn_error_of(errno, name);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_read.value, 0);
    posix_spawn_file_actions_adddup2(&actions, out_write.value, 1);
    posix_spawn_file_actions_adddup2(&actions, err_write.value, 2);
    std::string cwd;
    if (spec.cwd) {
        cwd = spec.cwd->string();
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    }
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    // Own process group (the whole tree is killed with kill(-pgid)), default SIGPIPE / SIGINT /
    // SIGTERM handling and an empty signal mask whatever the parent has set.
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGHUP);
    sigset_t no_mask;
    sigemptyset(&no_mask);
    posix_spawnattr_setflags(&attributes, static_cast<short>(POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK));
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setsigmask(&attributes, &no_mask);

    std::vector<char*> args;
    args.reserve(spec.argv.size() + 1);
    for (const auto& arg : spec.argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    auto environment = environment_strings(env);
    std::vector<char*> envp;
    envp.reserve(environment.size() + 1);
    for (auto& entry : environment) envp.push_back(entry.data());
    envp.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, program.c_str(), &actions, &attributes, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (spawned != 0) {
        result.spawn_error = spawn_error_of(spawned, name);
        return result;
    }
    in_read.reset();
    out_write.reset();
    err_write.reset();

    const SigpipeGuard sigpipe_guard;
    const std::string input = spec.stdin_data.value_or("");
    std::size_t written = 0;
    if (input.empty()) in_write.reset();
    else ::fcntl(in_write.value, F_SETFL, ::fcntl(in_write.value, F_GETFL) | O_NONBLOCK);

    const bool has_deadline = spec.timeout.count() > 0;
    const auto deadline = started + spec.timeout;
    Sink out_sink(spec.capture_stdout ? &result.out : nullptr, spec.max_stdout, false, nullptr);
    Sink err_sink(&result.err, spec.max_stderr, spec.stderr_tail_only, &result.err_dropped);
    bool killed = false;
    auto kill_tree = [&] {
        ::kill(-pid, SIGKILL);
        ::kill(pid, SIGKILL);
        killed = true;
    };
    // Returns false (after killing the tree) when the run must stop now.
    auto keep_going = [&]() {
        if (cancel != nullptr && cancel->load()) {
            result.cancelled = true;
            kill_tree();
            return false;
        }
        if (has_deadline && Clock::now() >= deadline) {
            result.timed_out = true;
            kill_tree();
            return false;
        }
        return true;
    };
    auto slice_ms = [&]() -> int {
        long long slice = cancel != nullptr ? 50 : 1000;
        if (has_deadline) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            slice = std::max<long long>(0, std::min<long long>(slice, left));
        }
        return static_cast<int>(slice);
    };

    char buffer[65536];
    while (!killed && (out_read.value >= 0 || err_read.value >= 0)) {
        if (!keep_going()) break;
        pollfd fds[3];
        nfds_t count = 0;
        if (out_read.value >= 0) fds[count++] = {out_read.value, POLLIN, 0};
        if (err_read.value >= 0) fds[count++] = {err_read.value, POLLIN, 0};
        if (in_write.value >= 0) fds[count++] = {in_write.value, POLLOUT, 0};
        const int ready = ::poll(fds, count, slice_ms());
        if (ready < 0) {
            if (errno == EINTR) continue;
            kill_tree();
            break;
        }
        for (nfds_t i = 0; i < count && !killed; ++i) {
            if (fds[i].revents == 0) continue;
            if (fds[i].fd == in_write.value) {
                const auto n = ::write(in_write.value, input.data() + written, input.size() - written);
                if (n > 0) written += static_cast<std::size_t>(n);
                // EPIPE: the child stopped reading (it may still exit 0); the rest is dropped.
                if ((n < 0 && errno != EAGAIN && errno != EINTR) || written >= input.size()) in_write.reset();
                continue;
            }
            const bool is_out = fds[i].fd == out_read.value;
            const auto n = ::read(fds[i].fd, buffer, sizeof buffer);
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (n <= 0) {
                (is_out ? out_read : err_read).reset();
                continue;
            }
            Sink& sink = is_out ? out_sink : err_sink;
            if (!sink.add(buffer, static_cast<std::size_t>(n))) {
                result.output_overflow = true;
                kill_tree();
            }
        }
    }
    in_write.reset();
    out_read.reset();
    err_read.reset();
    int status = 0;
    if (!killed) {
        // Both pipes closed: wait for the child itself, still under the deadline and the cancel.
        for (;;) {
            const pid_t done = ::waitpid(pid, &status, WNOHANG);
            if (done == pid) break;
            if (done < 0 && errno != EINTR) break;
            if (!keep_going()) break;
            ::usleep(static_cast<useconds_t>(std::min(slice_ms(), 10)) * 1000);
        }
    }
    if (killed) {
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        result.exit_code = -1;
        result.signal = SIGKILL;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = -1;
        result.signal = WTERMSIG(status);
    }
    err_sink.finish();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    return result;
}

#else

// --- Windows --------------------------------------------------------------------------------------

std::string signal_name(int signal) { return signal == 0 ? std::string() : "SIG" + std::to_string(signal); }

namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = nullptr;
    }
};

SpawnError spawn_error_of(DWORD code, const std::string& program) {
    SpawnErrorKind kind = SpawnErrorKind::os_error;
    std::string name = "EUNKNOWN";
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND || code == ERROR_DIRECTORY) {
        kind = SpawnErrorKind::not_found;
        name = "ENOENT";
    } else if (code == ERROR_ACCESS_DENIED) {
        kind = SpawnErrorKind::not_executable;
        name = "EACCES";
    } else if (code == ERROR_BAD_EXE_FORMAT || code == ERROR_EXE_MACHINE_TYPE_MISMATCH) {
        kind = SpawnErrorKind::not_executable;
        name = "ENOEXEC";
    }
    return SpawnError{kind, name, "spawn " + program + " " + name + " (Windows error " + std::to_string(code) + ")"};
}

/** The UTF-16 environment block, sorted by name (case-insensitive) as CreateProcess expects. */
std::wstring environment_block(const EnvList& env) {
    std::vector<std::wstring> entries;
    entries.reserve(env.size());
    for (const auto& [key, value] : env) entries.push_back(widen(key + "=" + value));
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        const auto name_a = a.substr(0, a.find(L'=', 1));
        const auto name_b = b.substr(0, b.find(L'=', 1));
        return CompareStringOrdinal(name_a.c_str(), static_cast<int>(name_a.size()), name_b.c_str(), static_cast<int>(name_b.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::wstring block;
    for (const auto& entry : entries) {
        block += entry;
        block.push_back(L'\0');
    }
    if (block.empty()) block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

} // namespace

EnvList current_environment() {
    EnvList out;
    LPWCH block = GetEnvironmentStringsW();
    if (block == nullptr) return out;
    for (LPWCH entry = block; *entry != L'\0'; entry += wcslen(entry) + 1) {
        const std::wstring_view text(entry);
        const auto eq = text.find(L'=', 1); // "=C:=C:\\dir" entries start with '='
        if (eq == std::wstring_view::npos || text[0] == L'=') continue;
        out.emplace_back(narrow(text.substr(0, eq)), narrow(text.substr(eq + 1)));
    }
    FreeEnvironmentStringsW(block);
    return out;
}

Result run(const Spec& spec, const std::atomic<bool>* cancel) {
    const auto started = Clock::now();
    Result result;
    const EnvList env = effective_environment(spec);
    if (auto invalid = validate(spec, env)) {
        result.spawn_error = std::move(invalid);
        return result;
    }
    const std::string& name = spec.argv[0];
    SearchRules rules;
    rules.windows = true;
    rules.path_list = env_get(env, "PATH").value_or("");
    rules.path_ext = env_get(env, "PATHEXT").value_or("");
    rules.is_executable = system_is_executable;
    auto program = find_executable_with(name, rules);
    if (!program) {
        result.spawn_error = spawn_error_of(ERROR_FILE_NOT_FOUND, name);
        return result;
    }
    if (is_shell_script_name(*program)) {
        result.spawn_error = SpawnError{SpawnErrorKind::refused_script, "EINVAL", "spawn " + name + ": a .bat / .cmd file runs through cmd.exe and is refused"};
        return result;
    }

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle in_read, in_write, out_read, out_write, err_read, err_write;
    if (!CreatePipe(&in_read.value, &in_write.value, &inherit, 0) || !CreatePipe(&out_read.value, &out_write.value, &inherit, 0) ||
        !CreatePipe(&err_read.value, &err_write.value, &inherit, 0)) {
        result.spawn_error = spawn_error_of(GetLastError(), name);
        return result;
    }
    SetHandleInformation(in_write.value, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read.value, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read.value, HANDLE_FLAG_INHERIT, 0);

    // The Job Object holds the whole tree: TerminateJobObject kills grandchildren too, and closing
    // the job (KILL_ON_JOB_CLOSE) does the same if this process dies first.
    Handle job;
    job.value = CreateJobObjectW(nullptr, nullptr);
    if (job.value == nullptr) {
        result.spawn_error = spawn_error_of(GetLastError(), name);
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof limits);

    // Only the three pipe ends are inherited (not every inheritable handle of this process).
    HANDLE inherited[3] = {in_read.value, out_write.value, err_write.value};
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<unsigned char> attribute_storage(attribute_size);
    auto* attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_size) ||
        !UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr, nullptr)) {
        result.spawn_error = spawn_error_of(GetLastError(), name);
        return result;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = in_read.value;
    startup.StartupInfo.hStdOutput = out_write.value;
    startup.StartupInfo.hStdError = err_write.value;
    startup.lpAttributeList = attribute_list;

    std::vector<std::string> argv = spec.argv;
    argv[0] = *program;
    std::wstring command = widen(windows_command_line(argv));
    const std::wstring application = widen(*program);
    std::wstring block = environment_block(env);
    const std::wstring cwd = spec.cwd ? spec.cwd->wstring() : std::wstring();
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
    if (spec.hide_window) flags |= CREATE_NO_WINDOW;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, TRUE, flags, block.data(),
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info);
    const DWORD create_error = GetLastError();
    DeleteProcThreadAttributeList(attribute_list);
    if (!created) {
        result.spawn_error = spawn_error_of(create_error, name);
        return result;
    }
    Handle process, thread;
    process.value = info.hProcess;
    thread.value = info.hThread;
    if (!AssignProcessToJobObject(job.value, process.value)) {
        const DWORD code = GetLastError();
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, INFINITE);
        result.spawn_error = spawn_error_of(code, name);
        return result;
    }
    ResumeThread(thread.value);
    thread.reset();
    in_read.reset();
    out_write.reset();
    err_write.reset();

    std::atomic<bool> overflow{false};
    std::atomic<int> readers_left{2};
    Sink out_sink(spec.capture_stdout ? &result.out : nullptr, spec.max_stdout, false, nullptr);
    Sink err_sink(&result.err, spec.max_stderr, spec.stderr_tail_only, &result.err_dropped);
    auto drain = [&](HANDLE pipe, Sink* sink) {
        std::vector<char> buffer(65536);
        DWORD n = 0;
        while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) && n > 0) {
            if (!sink->add(buffer.data(), n)) {
                overflow = true;
                TerminateJobObject(job.value, 1);
                break;
            }
        }
        readers_left.fetch_sub(1);
    };
    std::thread out_thread(drain, out_read.value, &out_sink);
    std::thread err_thread(drain, err_read.value, &err_sink);
    std::thread in_thread([&] {
        const std::string& input = spec.stdin_data ? *spec.stdin_data : std::string();
        std::size_t written = 0;
        while (written < input.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(input.size() - written, 1u << 20));
            DWORD n = 0;
            // ERROR_NO_DATA / ERROR_BROKEN_PIPE: the child stopped reading; the rest is dropped.
            if (!WriteFile(in_write.value, input.data() + written, chunk, &n, nullptr)) break;
            written += n;
        }
        in_write.reset();
    });

    const bool has_deadline = spec.timeout.count() > 0;
    const auto deadline = started + spec.timeout;
    bool killed = false;
    auto kill_tree = [&] {
        TerminateJobObject(job.value, 1);
        killed = true;
    };
    auto keep_going = [&]() {
        if (overflow) {
            result.output_overflow = true;
            kill_tree();
            return false;
        }
        if (cancel != nullptr && cancel->load()) {
            result.cancelled = true;
            kill_tree();
            return false;
        }
        if (has_deadline && Clock::now() >= deadline) {
            result.timed_out = true;
            kill_tree();
            return false;
        }
        return true;
    };
    // Wait for the child, then for the pipes to close (grandchildren may hold them), under the
    // deadline / cancel / overflow.
    bool exited = false;
    while (keep_going()) {
        if (!exited) exited = WaitForSingleObject(process.value, 20) == WAIT_OBJECT_0;
        else if (readers_left.load() == 0) break;
        else Sleep(10);
    }
    if (killed) {
        WaitForSingleObject(process.value, INFINITE);
        // Readers end once every write end is closed; a handle that escaped the job is cut off.
        for (int i = 0; i < 100 && readers_left.load() > 0; ++i) Sleep(10);
        if (readers_left.load() > 0) {
            CancelSynchronousIo(out_thread.native_handle());
            CancelSynchronousIo(err_thread.native_handle());
        }
    }
    CancelSynchronousIo(in_thread.native_handle());
    out_thread.join();
    err_thread.join();
    in_thread.join();
    if (overflow) result.output_overflow = true;
    DWORD code = 0;
    GetExitCodeProcess(process.value, &code);
    result.exit_code = killed ? -1 : static_cast<int>(code);
    err_sink.finish();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    return result;
}

#endif

} // namespace lapilli::process
