// The subscription CLI launch boundary (`claude -p` / `codex exec`), the C++ counterpart of the
// @ludiars/one-shot package (packages/one-shot). One job, one process, no retry (a lost answer must
// not run an agentic job twice), no shell, no API fallback:
//  - subscription_environment(): the child environment without API keys, cloud-provider selectors
//    or the parent session's bindings (the CLI uses its saved subscription login);
//  - resolve_model(): role names (opus / sonnet / haiku / fable / astra / sol / luna) resolve to the
//    central snapshot (LUDIARS_ONESHOT_MODEL_<ROLE> overrides it); explicit ids, context suffixes
//    such as "opus[1m]" and CLI-owned Codex ids ("codex-mini-latest") are kept;
//  - resolve_executable(): which file is spawned, by policy:
//      native_only       a native executable only. Windows: a .exe / .com on PATH, the native binary
//                        an npm package ships, or the .exe an npm .cmd shim points to; a shim to a
//                        script (the Node-based CLI) is refused. POSIX: an ELF / Mach-O file; a "#!"
//                        script is refused.
//      allow_node_entry  as the TS package: on Windows the npm package's JS entry runs as
//                        [node, entry]; elsewhere the command is spawned as found (scripts allowed).
//    A .cmd / .bat file is never spawned under either policy.
//  - prepare(): file + argv + env for one launch (also unwraps `cmd /c <cli> ...` without running
//    shell text); run(): prepare()d launch over lapilli::process (prompt on stdin, deadline,
//    output caps, cancellation, the whole process tree killed on timeout).
#pragma once

#include "lapilli/process.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::one_shot {

using process::EnvList;

enum class Provider { claude, codex };
/** "claude" / "codex". */
const char* provider_name(Provider provider) noexcept;

/** Errors of the launch rules (messages as the TS package's). */
class OneShotError : public std::runtime_error {
public:
    explicit OneShotError(const std::string& what) : std::runtime_error(what) {}
};

/** True for a variable the child must not see (API keys, cloud selectors, CLAUDECODE, CONCORDIA_HOOK, LICTOR_*). Case-insensitive. */
bool is_blocked_env_name(std::string_view name) noexcept;
/** `source` without the blocked variables (the source is not modified). */
EnvList subscription_environment(const EnvList& source);

struct RoleModel {
    const char* role;
    const char* model;
};
/** The role snapshot (Concordia's active catalog, 2026-10-03), in declaration order. */
const std::vector<RoleModel>& default_models();

/** resolveModel(roleOrId, provider, env). Throws OneShotError. */
std::string resolve_model(const std::optional<std::string>& role_or_id, Provider provider, const EnvList& env);
/** resolveModelArgs: rewrites `--model X` / `-m X` / `--model=X`, or inserts the default. Throws OneShotError. */
std::vector<std::string> resolve_model_args(std::vector<std::string> args, Provider provider, const EnvList& env);
/** claude for -p / --print, codex for exec; nullopt otherwise. */
std::optional<Provider> detect_provider(const std::vector<std::string>& args);

enum class Policy { native_only, allow_node_entry };

/** What resolve_executable may look at (injected for tests; system_lookup() for this process). */
struct ExecutableLookup {
    bool windows = false;
    /** PATH (Windows: ';'-separated, POSIX: ':'-separated). */
    std::string path_list;
    /** Windows PATHEXT (empty = ".COM;.EXE;.BAT;.CMD"). */
    std::string path_ext;
    /** "x64" / "arm64" (Windows npm package binaries). */
    std::string arch = "x64";
    /** Base directory for a relative command with a directory part. */
    std::string cwd;
    std::function<bool(const std::string&)> is_file;
    /** A shim's text (bounded by the implementation; empty when unreadable). */
    std::function<std::string(const std::string&)> read_text;
    /** The first `bytes` bytes of a file (empty when unreadable). */
    std::function<std::string(const std::string&, std::size_t)> read_head;
};

/** The lookup of this process (its platform, `env`'s PATH / PATHEXT, the file system; shims read within 1 MiB). */
ExecutableLookup system_lookup(const EnvList& env);

struct ResolvedExecutable {
    std::string file;
    std::vector<std::string> prefix_args;
    bool operator==(const ResolvedExecutable&) const = default;
};

enum class ResolutionErrorKind {
    not_found,          // nothing on PATH
    not_native,         // POSIX: neither ELF nor Mach-O
    script,             // the Node-based CLI (a "#!" script, a shim to a .js) under native_only
    shim_without_target,// a .cmd shim that names no "%dp0%\..." target
    shim_target_missing,// the shim's target does not exist
    unspawnable,        // .cmd / .bat (or a target of another kind) that would need a shell
    node_missing,       // allow_node_entry: the JS entry exists but node is not on PATH
};

class ResolutionError : public OneShotError {
public:
    ResolutionError(ResolutionErrorKind kind, std::string path, const std::string& what)
        : OneShotError(what), kind_(kind), path_(std::move(path)) {}
    [[nodiscard]] ResolutionErrorKind kind() const noexcept { return kind_; }
    /** The file the decision was about (empty for not_found). */
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    ResolutionErrorKind kind_;
    std::string path_;
};

/** Which file `command` is spawned as. Throws ResolutionError. */
ResolvedExecutable resolve_executable(const std::string& command, const ExecutableLookup& lookup, Policy policy);

/** ELF or Mach-O (32 / 64 bit, either byte order, universal) by the first four bytes. */
bool is_native_executable_head(std::string_view head) noexcept;

struct Launch {
    Provider provider = Provider::claude;
    std::string file;
    std::vector<std::string> args;
    EnvList env;
};

struct PrepareOptions {
    Policy policy = Policy::native_only;
    /** The child's base environment (unset: this process's). */
    std::optional<EnvList> env;
    /** File lookup (unset: system_lookup(env)). */
    std::optional<ExecutableLookup> lookup;
};

/** prepareOneShot(file, args, options). Throws OneShotError / ResolutionError. */
Launch prepare(std::string file, std::vector<std::string> args, const PrepareOptions& options = {});

struct RunOptions {
    std::optional<std::filesystem::path> cwd;
    std::chrono::milliseconds timeout{std::chrono::minutes(5)};
    std::size_t max_stdout = 8u << 20;
    std::size_t max_stderr = 1u << 20;
    /** Keep only the last max_stderr bytes of stderr (the usual diagnostic tail). */
    bool stderr_tail_only = true;
};

/** Runs a prepared launch once with `prompt` on stdin. Never retries. */
process::Result run(const Launch& launch, std::string_view prompt, const RunOptions& options = {}, const std::atomic<bool>* cancel = nullptr);

} // namespace lapilli::one_shot
