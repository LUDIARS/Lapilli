// lapilli::one_shot: the TS package's model / environment tests (packages/one-shot/src/models.test.js),
// Ingenium's native-only resolution tests (tests/playtest/resolve_cli_command_test.cpp), the TS
// allow_node_entry resolution, prepare() (cmd /c unwrapping, provider detection) and run().
#include "lapilli/one_shot.hpp"
#include "lapilli/test/check.hpp"

#include <map>
#include <memory>

using namespace lapilli::one_shot;
using Args = std::vector<std::string>;

namespace {

const std::string EXE_SHIM = "@ECHO off\r\nGOTO start\r\n:find_dp0\r\nSET dp0=%~dp0\r\nEXIT /b\r\n:start\r\nSETLOCAL\r\nCALL :find_dp0\r\n\"%dp0%\\node_modules\\tool\\bin\\tool.exe\"   %*";
const std::string SCRIPT_SHIM =
    "@ECHO off\nGOTO start\n:find_dp0\nSET dp0=%~dp0\nEXIT /b\n:start\nSETLOCAL\nCALL :find_dp0\n\nIF EXIST \"%dp0%\\node.exe\" (\n  SET \"_prog=%dp0%\\node.exe\"\n) ELSE (\n  SET \"_prog=node\"\n)\n\n"
    "endLocal & goto #_undefined_# 2>NUL || title %COMSPEC% & \"%_prog%\"  \"%dp0%\\node_modules\\tool\\dist\\index.js\" %*";

ExecutableLookup windows(std::map<std::string, std::string> files, std::string path_list = "C:\\bin;C:\\npm") {
    auto shared = std::make_shared<std::map<std::string, std::string>>(std::move(files));
    ExecutableLookup lookup;
    lookup.windows = true;
    lookup.path_list = std::move(path_list);
    lookup.path_ext = ".COM;.EXE;.BAT;.CMD";
    lookup.cwd = "C:\\work";
    lookup.is_file = [shared](const std::string& path) { return shared->count(path) > 0; };
    lookup.read_text = [shared](const std::string& path) {
        auto found = shared->find(path);
        return found == shared->end() ? std::string() : found->second;
    };
    return lookup;
}

ExecutableLookup posix(std::map<std::string, std::string> files) {
    auto shared = std::make_shared<std::map<std::string, std::string>>(std::move(files));
    ExecutableLookup lookup;
    lookup.path_list = "/usr/bin:/home/u/.local/bin";
    lookup.cwd = "/work";
    lookup.is_file = [shared](const std::string& path) { return shared->count(path) > 0; };
    lookup.read_head = [shared](const std::string& path, std::size_t bytes) {
        auto found = shared->find(path);
        return found == shared->end() ? std::string() : found->second.substr(0, bytes);
    };
    return lookup;
}

ResolutionErrorKind kind_of(const std::function<void()>& run) {
    try {
        run();
    } catch (const ResolutionError& error) {
        return error.kind();
    }
    throw lapilli::test::Failure("no ResolutionError");
}

std::string message_of(const std::function<void()>& run) {
    try {
        run();
    } catch (const std::exception& error) {
        return error.what();
    }
    return "";
}

const std::string ELF = std::string("\x7f" "ELF\x02\x01", 6);

} // namespace

LAPILLI_TEST("role defaults are concrete while explicitly pinned models stay exact (models.test.js)") {
    LAPILLI_CHECK(resolve_model_args({"-p", "--model", "opus"}, Provider::claude, {}) == (Args{"-p", "--model", "claude-opus-5-5"}));
    LAPILLI_CHECK(resolve_model_args({"exec", "-m", "gpt-6-luna", "-"}, Provider::codex, {}) == (Args{"exec", "-m", "gpt-6-luna", "-"}));
    LAPILLI_CHECK(resolve_model_args({"exec", "-"}, Provider::codex, {}) == (Args{"exec", "--model", "gpt-6-sol", "-"}));
    LAPILLI_CHECK(resolve_model_args({"-p"}, Provider::claude, {}) == (Args{"--model", "claude-sonnet-5", "-p"}));
    LAPILLI_CHECK(resolve_model_args({"-p", "--model=haiku"}, Provider::claude, {}) == (Args{"-p", "--model=claude-haiku-4-5-20251001"}));
    LAPILLI_CHECK_EQ(message_of([] { resolve_model_args({"-p", "--model"}, Provider::claude, {}); }), std::string("Missing model argument"));
    LAPILLI_CHECK_EQ(message_of([] { resolve_model_args({"-p", "--model", "gpt-6-sol"}, Provider::claude, {}); }), std::string("Claude requires a Claude model"));
}

LAPILLI_TEST("context suffixes, overrides and explicit Codex identifiers survive (models.test.js)") {
    LAPILLI_CHECK_EQ(resolve_model(std::string("opus[1m]"), Provider::claude, {}), std::string("claude-opus-5-5[1m]"));
    LAPILLI_CHECK_EQ(resolve_model(std::string("claude-opus-4-7[1m]"), Provider::claude, {}), std::string("claude-opus-4-7[1m]"));
    LAPILLI_CHECK_EQ(resolve_model(std::string("codex-mini-latest"), Provider::codex, {}), std::string("codex-mini-latest"));
    LAPILLI_CHECK_EQ(resolve_model(std::string("5.3-codex"), Provider::codex, {}), std::string("5.3-codex"));
    LAPILLI_CHECK_EQ(message_of([] { resolve_model(std::string("opus"), Provider::codex, {}); }), std::string("Codex requires a Codex model"));
    LAPILLI_CHECK_EQ(message_of([] { resolve_model(std::string("claude-opus-5-5[1m]; echo x"), Provider::claude, {}); }), std::string("Invalid one-shot model"));
    LAPILLI_CHECK_EQ(resolve_model(std::string("sonnet"), Provider::claude, {{"LUDIARS_ONESHOT_MODEL_SONNET", "claude-pinned-1"}}), std::string("claude-pinned-1"));
    LAPILLI_CHECK_EQ(resolve_model(std::string("sonnet[1m]"), Provider::claude, {{"LUDIARS_ONESHOT_MODEL_SONNET", "claude-x[200k]"}}), std::string("claude-x[200k]"));
    LAPILLI_CHECK_EQ(resolve_model(std::nullopt, Provider::codex, {{"LUDIARS_ONESHOT_MODEL_SOL", ""}}), std::string("gpt-6-sol"));
    LAPILLI_CHECK_EQ(default_models().size(), 7u);
}

LAPILLI_TEST("subscription environment drops API billing selectors and the parent session's bindings") {
    const EnvList source{{"PATH", "/bin"}, {"CODEX_HOME", "/account"}, {"CODEX_API_KEY", "test"}, {"ANTHROPIC_API_KEY", "test"},
                         {"LICTOR_PORT", "123"}, {"CONCORDIA_HOOK", "1"}, {"claudecode", "1"}, {"anthropic_base_url", "x"}};
    LAPILLI_CHECK(subscription_environment(source) == (EnvList{{"PATH", "/bin"}, {"CODEX_HOME", "/account"}}));
    LAPILLI_CHECK(is_blocked_env_name("Lictor_X") && !is_blocked_env_name("LICTO") && !is_blocked_env_name("ANTHROPIC_API_KEYS"));
}

LAPILLI_TEST("native_only off Windows: an ELF / Mach-O on PATH is spawned; a script or other file is refused") {
    LAPILLI_CHECK(resolve_executable("claude", posix({{"/home/u/.local/bin/claude", ELF}}), Policy::native_only) == (ResolvedExecutable{"/home/u/.local/bin/claude", {}}));
    LAPILLI_CHECK(resolve_executable("claude", posix({{"/usr/bin/claude", std::string("\xcf\xfa\xed\xfe", 4)}}), Policy::native_only) == (ResolvedExecutable{"/usr/bin/claude", {}}));
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", posix({{"/usr/bin/claude", "#!/usr/bin/env node\n"}}), Policy::native_only); }) == ResolutionErrorKind::script);
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", posix({}), Policy::native_only); }) == ResolutionErrorKind::not_found);
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", posix({{"/usr/bin/claude", "MZ"}}), Policy::native_only); }) == ResolutionErrorKind::not_native);
    LAPILLI_CHECK(resolve_executable("bin/claude", posix({{"/work/bin/claude", ELF}}), Policy::native_only) == (ResolvedExecutable{"/work/bin/claude", {}}));
    // allow_node_entry keeps the TS behaviour: the command is spawned as given.
    LAPILLI_CHECK(resolve_executable("claude", posix({}), Policy::allow_node_entry) == (ResolvedExecutable{"claude", {}}));
}

LAPILLI_TEST("native_only on Windows: exe, the npm package binary, or a shim's native target") {
    const auto shim = windows({{"C:\\npm\\claude.cmd", EXE_SHIM}, {"C:\\npm\\node_modules\\tool\\bin\\tool.exe", ""}});
    LAPILLI_CHECK(resolve_executable("claude", shim, Policy::native_only) == (ResolvedExecutable{"C:\\npm\\node_modules\\tool\\bin\\tool.exe", {}}));
    const auto order = windows({{"C:\\bin\\claude.cmd", EXE_SHIM}, {"C:\\bin\\node_modules\\tool\\bin\\tool.exe", ""}, {"C:\\bin\\claude.exe", ""}, {"C:\\npm\\claude.exe", ""}});
    LAPILLI_CHECK(resolve_executable("claude", order, Policy::native_only) == (ResolvedExecutable{"C:\\bin\\claude.exe", {}}));
    const auto package = windows({{"C:\\npm\\claude.cmd", SCRIPT_SHIM}, {"C:\\npm\\node_modules\\@anthropic-ai\\claude-code\\bin\\claude.exe", ""}});
    LAPILLI_CHECK(resolve_executable("claude", package, Policy::native_only) == (ResolvedExecutable{"C:\\npm\\node_modules\\@anthropic-ai\\claude-code\\bin\\claude.exe", {}}));
    const auto codex = windows({{"C:\\npm\\node_modules\\@openai\\codex-win32-x64\\vendor\\x86_64-pc-windows-msvc\\codex\\codex.exe", ""}});
    LAPILLI_CHECK(resolve_executable("codex", codex, Policy::native_only).file == "C:\\npm\\node_modules\\@openai\\codex-win32-x64\\vendor\\x86_64-pc-windows-msvc\\codex\\codex.exe");
    LAPILLI_CHECK(resolve_executable("C:\\tools\\claude.exe", windows({{"C:\\tools\\claude.exe", ""}}), Policy::native_only).file == "C:\\tools\\claude.exe");
}

LAPILLI_TEST("Windows refusals carry their reason; the node.exe probe in a shim is not its target") {
    const auto script = windows({{"C:\\npm\\claude.cmd", SCRIPT_SHIM}, {"C:\\npm\\node_modules\\tool\\dist\\index.js", ""}, {"C:\\npm\\node.exe", ""}});
    LAPILLI_CHECK(kind_of([&] { resolve_executable("claude", script, Policy::native_only); }) == ResolutionErrorKind::script);
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", windows({}), Policy::native_only); }) == ResolutionErrorKind::not_found);
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", windows({{"C:\\npm\\claude.cmd", "@ECHO off"}}), Policy::native_only); }) == ResolutionErrorKind::shim_without_target);
    LAPILLI_CHECK(kind_of([] { resolve_executable("claude", windows({{"C:\\npm\\claude.cmd", EXE_SHIM}}), Policy::native_only); }) == ResolutionErrorKind::shim_target_missing);
    LAPILLI_CHECK(kind_of([] { resolve_executable("C:\\x\\run.bat", windows({}), Policy::allow_node_entry); }) == ResolutionErrorKind::unspawnable);
    LAPILLI_CHECK(kind_of([] { resolve_executable("C:\\x\\run.bat", windows({}), Policy::native_only); }) == ResolutionErrorKind::unspawnable);
}

LAPILLI_TEST("allow_node_entry on Windows: the npm JS entry runs as [node, entry] (TS resolveExecutable)") {
    const auto entry = windows({{"C:\\npm\\node_modules\\@anthropic-ai\\claude-code\\cli.js", ""}, {"C:\\bin\\node.exe", ""}});
    LAPILLI_CHECK(resolve_executable("claude", entry, Policy::allow_node_entry) ==
                  (ResolvedExecutable{"C:\\bin\\node.exe", {"C:\\npm\\node_modules\\@anthropic-ai\\claude-code\\cli.js"}}));
    const auto shim = windows({{"C:\\npm\\claude.cmd", SCRIPT_SHIM}, {"C:\\npm\\node_modules\\tool\\dist\\index.js", ""}, {"C:\\npm\\node.exe", ""}});
    LAPILLI_CHECK(resolve_executable("claude", shim, Policy::allow_node_entry) == (ResolvedExecutable{"C:\\npm\\node.exe", {"C:\\npm\\node_modules\\tool\\dist\\index.js"}}));
    const auto no_node = windows({{"C:\\npm\\node_modules\\@openai\\codex\\bin\\codex.js", ""}});
    LAPILLI_CHECK(kind_of([&] { resolve_executable("codex", no_node, Policy::allow_node_entry); }) == ResolutionErrorKind::node_missing);
    LAPILLI_CHECK(resolve_executable("claude", windows({}), Policy::allow_node_entry) == (ResolvedExecutable{"claude", {}}));
}

LAPILLI_TEST("prepare: provider detection, cmd /c unwrapping, model args, subscription env, Git Bash on Windows") {
    PrepareOptions options;
    options.env = EnvList{{"PATH", "/usr/bin:/home/u/.local/bin"}, {"ANTHROPIC_API_KEY", "k"}};
    options.lookup = posix({{"/home/u/.local/bin/claude", ELF}, {"/usr/bin/codex", ELF}});
    const auto launch = prepare("claude", {"-p", "--model", "opus", "--tools", ""}, options);
    LAPILLI_CHECK(launch.provider == Provider::claude);
    LAPILLI_CHECK_EQ(launch.file, std::string("/home/u/.local/bin/claude"));
    LAPILLI_CHECK(launch.args == (Args{"-p", "--model", "claude-opus-5-5", "--tools", ""}));
    LAPILLI_CHECK(launch.env == (EnvList{{"PATH", "/usr/bin:/home/u/.local/bin"}}));
    const auto codex = prepare("codex", {"exec", "--json", "-"}, options);
    LAPILLI_CHECK(codex.args == (Args{"exec", "--model", "gpt-6-sol", "--json", "-"}));
    const auto wrapped = prepare("C:\\Windows\\System32\\cmd.exe", {"/C", "claude", "-p"}, options);
    LAPILLI_CHECK_EQ(wrapped.file, std::string("/home/u/.local/bin/claude"));
    LAPILLI_CHECK_EQ(message_of([&] { prepare("cmd", {"/c", "claude && evil", "-p"}, options); }), std::string("Unsupported CLI shell wrapper"));
    LAPILLI_CHECK_EQ(message_of([&] { prepare("claude", {"--version"}, options); }), std::string("One-shot launch requires claude print mode or codex exec"));
    LAPILLI_CHECK_EQ(message_of([&] { prepare("claude", {"-p", "--model", "bad model"}, options); }), std::string("Invalid one-shot model"));
    PrepareOptions win;
    win.env = EnvList{{"Path", "C:\\bin"}};
    win.lookup = windows({{"C:\\bin\\claude.exe", ""}, {"C:/Program Files/Git/bin/bash.exe", ""}});
    const auto on_windows = prepare("claude", {"-p"}, win);
    LAPILLI_CHECK_EQ(on_windows.file, std::string("C:\\bin\\claude.exe"));
    LAPILLI_CHECK_EQ(lapilli::process::env_get(on_windows.env, "CLAUDE_CODE_GIT_BASH_PATH").value_or(""), std::string("C:/Program Files/Git/bin/bash.exe"));
}

LAPILLI_TEST("native executable heads") {
    LAPILLI_CHECK(is_native_executable_head(ELF));
    LAPILLI_CHECK(is_native_executable_head(std::string("\xca\xfe\xba\xbe", 4)));
    LAPILLI_CHECK(!is_native_executable_head("#!/b"));
    LAPILLI_CHECK(!is_native_executable_head("\x7f" "EL"));
}

#ifndef _WIN32
LAPILLI_TEST("run: the prompt goes in on stdin, with the launch's environment only; the deadline holds") {
    Launch launch;
    launch.file = "sh";
    launch.args = {"-c", "cat; printf '|%s' \"${ANTHROPIC_API_KEY:-none}\""};
    launch.env = {{"PATH", "/usr/bin:/bin"}};
    const auto result = run(launch, "the prompt");
    LAPILLI_CHECK(result.ok());
    LAPILLI_CHECK_EQ(result.out, std::string("the prompt|none"));
    launch.args = {"-c", "sleep 30"};
    RunOptions options;
    options.timeout = std::chrono::milliseconds(200);
    LAPILLI_CHECK(run(launch, "", options).timed_out);
}
#endif
