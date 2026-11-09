// lapilli::process: argument vector (no shell), stdin, deadline, output caps (exact / one over /
// huge), process-tree kill on timeout, SIGPIPE safety, cancellation, environment modes, PATH
// search rules (POSIX and Windows rules run everywhere through the injected search).
#include "lapilli/process.hpp"
#include "lapilli/test/check.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

using namespace lapilli::process;

namespace {
Spec spec_of(std::vector<std::string> argv) {
    Spec spec;
    spec.argv = std::move(argv);
    return spec;
}
} // namespace

#ifndef _WIN32

LAPILLI_TEST("arguments reach the child as given, with no shell expansion") {
    const auto result = run(spec_of({"printf", "%s|", "a b", "$HOME", "*", ";echo injected", "`id`"}));
    LAPILLI_CHECK(result.ok());
    LAPILLI_CHECK_EQ(result.out, std::string("a b|$HOME|*|;echo injected|`id`|"));
}

LAPILLI_TEST("stdin is written, exit codes and signals are reported") {
    auto spec = spec_of({"cat"});
    spec.stdin_data = "hello";
    LAPILLI_CHECK_EQ(run(spec).out, std::string("hello"));
    const auto failed = run(spec_of({"false"}));
    LAPILLI_CHECK_EQ(failed.exit_code, 1);
    LAPILLI_CHECK(!failed.ok());
    const auto signalled = run(spec_of({"sh", "-c", "kill -TERM $$"}));
    LAPILLI_CHECK_EQ(signalled.exit_code, -1);
    LAPILLI_CHECK_EQ(signal_name(signalled.signal), std::string("SIGTERM"));
}

LAPILLI_TEST("a child that does not read a large stdin does not raise SIGPIPE in the parent") {
    auto spec = spec_of({"true"});
    spec.stdin_data = std::string(8u << 20, 'x');
    const auto result = run(spec);
    LAPILLI_CHECK_EQ(result.exit_code, 0);
    sigset_t pending;
    sigemptyset(&pending);
    sigpending(&pending);
    LAPILLI_CHECK(sigismember(&pending, SIGPIPE) != 1);
}

LAPILLI_TEST("the deadline kills the child and its grandchildren (process group)") {
    // The grandchild keeps stdout open; without a group kill the run would wait for it.
    auto spec = spec_of({"sh", "-c", "sleep 30 & echo $!; wait"});
    spec.timeout = std::chrono::milliseconds(300);
    const auto started = std::chrono::steady_clock::now();
    const auto result = run(spec);
    const auto took = std::chrono::steady_clock::now() - started;
    LAPILLI_CHECK(result.timed_out);
    LAPILLI_CHECK_EQ(result.exit_code, -1);
    LAPILLI_CHECK_EQ(result.signal, SIGKILL);
    LAPILLI_CHECK(took < std::chrono::seconds(5));
    const auto grandchild = std::stoi(result.out);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Gone, or a zombie waiting for init to reap it (containers' init may not reap promptly).
    bool alive = ::kill(grandchild, 0) == 0;
    if (alive) {
        std::ifstream stat("/proc/" + std::to_string(grandchild) + "/stat");
        std::string pid_text, name, state;
        if (stat >> pid_text >> name >> state) alive = state != "Z" && state != "X";
    }
    LAPILLI_CHECK(!alive);
}

LAPILLI_TEST("a grandchild holding the pipe after the child exited is bounded by the deadline") {
    auto spec = spec_of({"sh", "-c", "(sleep 30) & exit 0"});
    spec.timeout = std::chrono::milliseconds(300);
    const auto result = run(spec);
    LAPILLI_CHECK(result.timed_out);
}

LAPILLI_TEST("boundary: stdout at the cap is kept, one byte over kills the run; huge output is refused") {
    auto spec = spec_of({"printf", "abcd"});
    spec.max_stdout = 4;
    LAPILLI_CHECK_EQ(run(spec).out, std::string("abcd"));
    spec.argv = {"printf", "abcde"};
    const auto over = run(spec);
    LAPILLI_CHECK(over.output_overflow);
    LAPILLI_CHECK_EQ(over.exit_code, -1);
    auto huge = spec_of({"head", "-c", "100000000", "/dev/zero"});
    huge.max_stdout = 1u << 20;
    const auto refused = run(huge);
    LAPILLI_CHECK(refused.output_overflow);
    LAPILLI_CHECK(refused.out.size() <= (1u << 20));
    auto err = spec_of({"sh", "-c", "printf 12345 >&2"});
    err.max_stderr = 4;
    LAPILLI_CHECK(run(err).output_overflow);
}

LAPILLI_TEST("stderr tail mode keeps the last bytes and never overflows") {
    auto spec = spec_of({"sh", "-c", "head -c 300000 /dev/zero | tr '\\0' a >&2; printf END >&2"});
    spec.max_stderr = 10;
    spec.stderr_tail_only = true;
    const auto result = run(spec);
    LAPILLI_CHECK(result.ok());
    LAPILLI_CHECK_EQ(result.err, std::string("aaaaaaaEND"));
    LAPILLI_CHECK_EQ(result.err_dropped, 300003u - 10u);
}

LAPILLI_TEST("cancellation kills the tree") {
    std::atomic<bool> cancel{false};
    std::thread trigger([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel = true;
    });
    const auto result = run(spec_of({"sleep", "30"}), &cancel);
    trigger.join();
    LAPILLI_CHECK(result.cancelled);
    LAPILLI_CHECK(!result.timed_out);
    LAPILLI_CHECK(result.duration < std::chrono::seconds(5));
}

LAPILLI_TEST("environment modes: overlay, removal, replace, empty; cwd") {
    ::setenv("LAPILLI_TEST_KEEP", "kept", 1);
    ::setenv("LAPILLI_TEST_DROP", "secret", 1);
    auto spec = spec_of({"sh", "-c", "printf '%s|%s|%s' \"$LAPILLI_TEST_KEEP\" \"$LAPILLI_TEST_DROP\" \"$LAPILLI_TEST_ADD\""});
    spec.env = {{"LAPILLI_TEST_ADD", "added"}};
    spec.env_remove = {"LAPILLI_TEST_DROP"};
    LAPILLI_CHECK_EQ(run(spec).out, std::string("kept||added"));
    spec.env_mode = EnvMode::replace;
    spec.env = {{"PATH", "/usr/bin:/bin"}, {"LAPILLI_TEST_ADD", "only"}};
    LAPILLI_CHECK_EQ(run(spec).out, std::string("||only"));
    auto empty = spec_of({"/usr/bin/env"});
    empty.env_mode = EnvMode::empty;
    LAPILLI_CHECK_EQ(run(empty).out, std::string(""));
    auto cwd = spec_of({"pwd"});
    cwd.cwd = "/";
    LAPILLI_CHECK_EQ(run(cwd).out, std::string("/\n"));
    LAPILLI_CHECK_EQ(env_get(current_environment(), "LAPILLI_TEST_KEEP").value_or(""), std::string("kept"));
}

LAPILLI_TEST("spawn failures are reported, not thrown") {
    const auto missing = run(spec_of({"lapilli-no-such-program-xyz"}));
    LAPILLI_CHECK(missing.spawn_error.has_value());
    LAPILLI_CHECK(missing.spawn_error->kind == SpawnErrorKind::not_found);
    LAPILLI_CHECK_EQ(missing.spawn_error->errno_name, std::string("ENOENT"));
    const auto plain_file = std::filesystem::temp_directory_path() / ("lapilli-not-exec-" + std::to_string(::getpid()));
    { std::ofstream(plain_file) << "data"; }
    const auto not_exec = run(spec_of({plain_file.string()}));
    std::filesystem::remove(plain_file);
    LAPILLI_CHECK(not_exec.spawn_error.has_value());
    LAPILLI_CHECK(not_exec.spawn_error->kind == SpawnErrorKind::not_executable);
    LAPILLI_CHECK(run(spec_of({})).spawn_error->kind == SpawnErrorKind::invalid_argument);
    LAPILLI_CHECK(run(spec_of({"printf", std::string("a\0b", 3)})).spawn_error->kind == SpawnErrorKind::invalid_argument);
    LAPILLI_CHECK(find_executable("sh").has_value());
    LAPILLI_CHECK(!find_executable("lapilli-no-such-program-xyz").has_value());
}

#endif

LAPILLI_TEST("Windows arguments are quoted for CommandLineToArgvW") {
    LAPILLI_CHECK_EQ(quote_windows_argument("plain"), std::string("plain"));
    LAPILLI_CHECK_EQ(quote_windows_argument("a b"), std::string("\"a b\""));
    LAPILLI_CHECK_EQ(quote_windows_argument(""), std::string("\"\""));
    LAPILLI_CHECK_EQ(quote_windows_argument("say \"hi\""), std::string("\"say \\\"hi\\\"\""));
    LAPILLI_CHECK_EQ(quote_windows_argument("dir\\ x\\"), std::string("\"dir\\ x\\\\\""));
    LAPILLI_CHECK_EQ(quote_windows_argument("a&b|c"), std::string("a&b|c"));
    LAPILLI_CHECK_EQ(windows_command_line({"C:\\p q\\x.exe", "-p", ""}), std::string("\"C:\\p q\\x.exe\" -p \"\""));
    LAPILLI_CHECK(is_shell_script_name("C:\\npm\\claude.CMD") && is_shell_script_name("x.bat") && !is_shell_script_name("x.exe"));
}

LAPILLI_TEST("PATH search rules: Windows PATHEXT order inside PATH order; POSIX never the current directory") {
    std::map<std::string, bool> files{{"C:\\a\\tool.cmd", true}, {"C:\\b\\tool.exe", true}, {"C:\\a\\x.exe", true}, {"/usr/bin/tool", true}};
    SearchRules windows;
    windows.windows = true;
    windows.path_list = "C:\\a;;C:\\b\\";
    windows.is_executable = [&](const std::string& path) { return files.count(path) > 0; };
    LAPILLI_CHECK_EQ(find_executable_with("tool", windows).value_or(""), std::string("C:\\a\\tool.cmd"));
    windows.path_ext = ".EXE";
    LAPILLI_CHECK_EQ(find_executable_with("tool", windows).value_or(""), std::string("C:\\b\\tool.exe"));
    LAPILLI_CHECK_EQ(find_executable_with("x.exe", windows).value_or(""), std::string("C:\\a\\x.exe"));
    LAPILLI_CHECK_EQ(find_executable_with("C:\\a\\x", windows).value_or(""), std::string("C:\\a\\x.exe"));
    SearchRules posix;
    posix.path_list = ":/usr/bin";
    posix.is_executable = [&](const std::string& path) { return files.count(path) > 0; };
    LAPILLI_CHECK_EQ(find_executable_with("tool", posix).value_or(""), std::string("/usr/bin/tool"));
    LAPILLI_CHECK(!find_executable_with("missing", posix).has_value());
}
