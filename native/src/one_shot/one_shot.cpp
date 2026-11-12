#include "lapilli/one_shot.hpp"

#include "lapilli/fs.hpp"
#include "lapilli/text.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace lapilli::one_shot {

using text::ascii_iequals;
using text::ascii_lower;
using text::ascii_upper;
using text::ends_with;
using text::starts_with;

const char* provider_name(Provider provider) noexcept { return provider == Provider::claude ? "claude" : "codex"; }

// --- environment ----------------------------------------------------------------------------------

bool is_blocked_env_name(std::string_view name) noexcept {
    // /^(ANTHROPIC_API_KEY|ANTHROPIC_AUTH_TOKEN|ANTHROPIC_BASE_URL|OPENAI_API_KEY|OPENAI_BASE_URL|
    //    CODEX_API_KEY|CLAUDE_CODE_USE_BEDROCK|CLAUDE_CODE_USE_VERTEX|CLAUDE_CODE_USE_FOUNDRY|
    //    CLAUDE_CODE_USE_ANTHROPIC_AWS|CLAUDECODE|CONCORDIA_HOOK|LICTOR_.*)$/i
    static constexpr std::string_view exact[] = {
        "ANTHROPIC_API_KEY",       "ANTHROPIC_AUTH_TOKEN",   "ANTHROPIC_BASE_URL",           "OPENAI_API_KEY",
        "OPENAI_BASE_URL",         "CODEX_API_KEY",          "CLAUDE_CODE_USE_BEDROCK",      "CLAUDE_CODE_USE_VERTEX",
        "CLAUDE_CODE_USE_FOUNDRY", "CLAUDE_CODE_USE_ANTHROPIC_AWS", "CLAUDECODE",           "CONCORDIA_HOOK",
    };
    for (const auto& blocked : exact)
        if (ascii_iequals(name, blocked)) return true;
    return name.size() >= 7 && ascii_iequals(name.substr(0, 7), "LICTOR_");
}

EnvList subscription_environment(const EnvList& source) {
    EnvList out;
    for (const auto& entry : source)
        if (!is_blocked_env_name(entry.first)) out.push_back(entry);
    return out;
}

// --- models ---------------------------------------------------------------------------------------

const std::vector<RoleModel>& default_models() {
    // Snapshot of the active Concordia model catalog, 2026-10-03 (packages/one-shot/src/models.js).
    static const std::vector<RoleModel> models = {
        {"opus", "claude-opus-5-5"}, {"sonnet", "claude-sonnet-5"}, {"haiku", "claude-haiku-4-5-20251001"},
        {"fable", "claude-fable-5-1"}, {"astra", "gpt-6-astra"}, {"sol", "gpt-6-sol"}, {"luna", "gpt-6-luna"},
    };
    return models;
}

namespace {

bool is_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

/** Length of a trailing context suffix "[<digits>k|m]" (case-insensitive), or 0. */
std::size_t context_suffix_length(std::string_view text) {
    if (text.size() < 4 || text.back() != ']') return 0;
    const auto open = text.rfind('[');
    if (open == std::string_view::npos) return 0;
    const std::string_view inside = text.substr(open + 1, text.size() - open - 2);
    if (inside.size() < 2) return 0;
    const char unit = inside.back();
    if (unit != 'k' && unit != 'm' && unit != 'K' && unit != 'M') return 0;
    for (char c : inside.substr(0, inside.size() - 1))
        if (c < '0' || c > '9') return 0;
    return text.size() - open;
}

/** /^[a-zA-Z0-9][a-zA-Z0-9._:/-]*(?:\[\d+[km]\])?$/i */
bool valid_model(std::string_view model) {
    if (model.empty() || !is_alnum(model[0])) return false;
    std::string_view body = model.substr(0, model.size() - context_suffix_length(model));
    if (body.empty()) return false;
    return std::all_of(body.begin(), body.end(), [](char c) { return is_alnum(c) || c == '.' || c == '_' || c == ':' || c == '/' || c == '-'; });
}

const char* role_default(std::string_view role) {
    for (const auto& entry : default_models())
        if (role == entry.role) return entry.model;
    return nullptr;
}

} // namespace

std::string resolve_model(const std::optional<std::string>& role_or_id, Provider provider, const EnvList& env) {
    const std::string requested = role_or_id && !role_or_id->empty() ? *role_or_id : (provider == Provider::claude ? "sonnet" : "sol");
    // /^(.+?)(\[\d+[km]\])?$/i: the lazy role leaves at least one character before the suffix.
    std::size_t suffix = context_suffix_length(requested);
    if (suffix >= requested.size()) suffix = 0;
    const std::string role = requested.substr(0, requested.size() - suffix);
    const std::string context = requested.substr(requested.size() - suffix);
    const char* known = role_default(role);
    std::string model = requested;
    if (known != nullptr) {
        const auto override_value = process::env_get(env, "LUDIARS_ONESHOT_MODEL_" + ascii_upper(role));
        const std::string base = override_value && !override_value->empty() ? *override_value : std::string(known);
        model = base.find('[') == std::string::npos ? base + context : base;
    }
    if (!valid_model(model)) throw OneShotError("Invalid one-shot model");
    if (provider == Provider::claude && !starts_with(model, "claude-")) throw OneShotError("Claude requires a Claude model");
    // Codex accepts CLI-owned identifiers (codex-mini-latest); only Claude models are refused.
    if (provider == Provider::codex && starts_with(model, "claude-")) throw OneShotError("Codex requires a Codex model");
    return model;
}

std::vector<std::string> resolve_model_args(std::vector<std::string> args, Provider provider, const EnvList& env) {
    const auto index = std::find_if(args.begin(), args.end(), [](const std::string& a) { return a == "--model" || a == "-m"; });
    const auto inline_at = std::find_if(args.begin(), args.end(), [](const std::string& a) { return starts_with(a, "--model="); });
    if (index != args.end()) {
        if (index + 1 == args.end() || (index + 1)->empty()) throw OneShotError("Missing model argument");
        *(index + 1) = resolve_model(*(index + 1), provider, env);
    } else if (inline_at != args.end()) {
        *inline_at = "--model=" + resolve_model(inline_at->substr(8), provider, env);
    } else {
        // Before the stdin sentinel rather than after a positional prompt (indexOf('exec') + 1).
        std::ptrdiff_t position = 0;
        if (provider == Provider::codex) {
            const auto exec = std::find(args.begin(), args.end(), "exec");
            position = exec == args.end() ? 0 : (exec - args.begin()) + 1;
        }
        args.insert(args.begin() + position, {"--model", resolve_model(std::nullopt, provider, env)});
    }
    return args;
}

std::optional<Provider> detect_provider(const std::vector<std::string>& args) {
    if (std::find(args.begin(), args.end(), "exec") != args.end()) return Provider::codex;
    if (std::find(args.begin(), args.end(), "-p") != args.end() || std::find(args.begin(), args.end(), "--print") != args.end()) return Provider::claude;
    return std::nullopt;
}

// --- executable resolution ------------------------------------------------------------------------

namespace {

namespace win32 {

bool is_sep(char c) { return c == '\\' || c == '/'; }

std::string extname(const std::string& path) {
    std::size_t start = path.size();
    while (start > 0 && !is_sep(path[start - 1]) && path[start - 1] != ':') --start;
    const std::string name = path.substr(start);
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    return name.substr(dot);
}

std::string basename(const std::string& path) {
    std::size_t start = path.size();
    while (start > 0 && !is_sep(path[start - 1])) --start;
    return path.substr(start);
}

bool is_absolute(const std::string& path) {
    if (!path.empty() && is_sep(path[0])) return true;
    return path.size() >= 3 && path[1] == ':' && is_sep(path[2]);
}

std::string normalize(const std::string& path) {
    std::string prefix;
    std::string rest = path;
    if (rest.size() >= 2 && rest[1] == ':') {
        prefix = rest.substr(0, 2);
        rest = rest.substr(2);
    }
    const bool rooted = !rest.empty() && is_sep(rest[0]);
    std::vector<std::string> parts;
    std::string current;
    auto flush = [&] {
        if (current == "..") {
            if (!parts.empty() && parts.back() != "..") parts.pop_back();
            else if (!rooted) parts.push_back("..");
        } else if (!current.empty() && current != ".") {
            parts.push_back(current);
        }
        current.clear();
    };
    for (char c : rest) {
        if (is_sep(c)) flush();
        else current += c;
    }
    flush();
    std::string out = prefix + (rooted ? "\\" : "");
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : "\\") + parts[i];
    if (out.empty()) out = ".";
    return out;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return normalize(b);
    if (b.empty()) return normalize(a);
    return normalize(a + "\\" + b);
}

std::string dirname(const std::string& path) {
    const auto at = path.find_last_of("\\/");
    if (at == std::string::npos) return path.size() >= 2 && path[1] == ':' ? path.substr(0, 2) : ".";
    if (at == 0) return path.substr(0, 1);
    if (at == 2 && path[1] == ':') return path.substr(0, 3);
    return path.substr(0, at);
}

} // namespace win32

bool ext_in(const std::string& extension, std::initializer_list<const char*> set) {
    for (const char* item : set)
        if (ascii_iequals(extension, item)) return true;
    return false;
}

/** The last "%dp0%\<path>" an npm .cmd shim names (the target it runs), or empty. */
std::string shim_target_text(const std::string& text) {
    static constexpr std::string_view marker = "\"%dp0%\\";
    std::string target;
    std::size_t at = 0;
    while ((at = text.find(marker, at)) != std::string::npos) {
        const auto start = at + marker.size();
        const auto end = text.find('"', start);
        if (end == std::string::npos) break;
        if (end > start) target = text.substr(start, end - start);
        at = end + 1;
    }
    return target;
}

std::optional<std::string> find_node(const ExecutableLookup& lookup) {
    process::SearchRules rules;
    rules.windows = lookup.windows;
    rules.path_list = lookup.path_list;
    rules.path_ext = lookup.windows ? ".EXE;.COM" : "";
    rules.is_executable = lookup.is_file;
    return process::find_executable_with("node", rules);
}

ResolvedExecutable node_entry(const std::string& entry, const ExecutableLookup& lookup) {
    const auto node = find_node(lookup);
    if (!node) throw ResolutionError(ResolutionErrorKind::node_missing, entry, entry + " needs node, which is not on PATH");
    return {*node, {entry}};
}

ResolvedExecutable resolve_windows(const std::string& command, const ExecutableLookup& lookup, Policy policy) {
    const bool has_dir = win32::is_absolute(command) || command.find_first_of("/\\") != std::string::npos;
    std::vector<std::string> dirs;
    if (has_dir) {
        dirs.push_back(win32::dirname(win32::is_absolute(command) ? win32::normalize(command) : win32::join(lookup.cwd, command)));
    } else {
        for (const auto& dir : text::split(lookup.path_list, ';'))
            if (!dir.empty()) dirs.push_back(dir);
    }
    std::string name = win32::basename(command);
    if (ext_in(win32::extname(name), {".cmd", ".exe"})) name = name.substr(0, name.size() - 4);
    std::vector<std::string> extensions;
    for (const auto& ext : text::split(lookup.path_ext.empty() ? ".COM;.EXE;.BAT;.CMD" : lookup.path_ext, ';'))
        if (!ext.empty()) extensions.push_back(ascii_lower(ext));
    const std::string triple = lookup.arch == "arm64" ? "aarch64-pc-windows-msvc" : "x86_64-pc-windows-msvc";

    for (const auto& dir : dirs) {
        // 1. A native executable of that name (PATHEXT order).
        for (const auto& ext : extensions)
            if (ext_in(ext, {".exe", ".com"}) && lookup.is_file(win32::join(dir, name + ext))) return {win32::join(dir, name + ext), {}};
        // 2. The native binary an npm package ships next to its shim.
        if (name == "claude") {
            const auto binary = win32::join(dir, "node_modules\\@anthropic-ai\\claude-code\\bin\\claude.exe");
            if (lookup.is_file(binary)) return {binary, {}};
        }
        if (name == "codex") {
            for (const char* package : {"codex", lookup.arch == "arm64" ? "codex-win32-arm64" : "codex-win32-x64"}) {
                const auto binary = win32::join(dir, std::string("node_modules\\@openai\\") + package + "\\vendor\\" + triple + "\\codex\\codex.exe");
                if (lookup.is_file(binary)) return {binary, {}};
            }
        }
        // 3. An npm .cmd shim: spawn its target, never the shim (that would start cmd.exe).
        for (const auto& ext : extensions) {
            if (!ext_in(ext, {".cmd", ".bat"})) continue;
            const std::string shim = win32::join(dir, name + ext);
            if (!lookup.is_file(shim)) continue;
            const std::string target = shim_target_text(lookup.read_text ? lookup.read_text(shim) : std::string());
            if (target.empty()) throw ResolutionError(ResolutionErrorKind::shim_without_target, shim, shim + " names no target to spawn");
            const std::string file = win32::join(win32::dirname(shim), target);
            if (!lookup.is_file(file)) throw ResolutionError(ResolutionErrorKind::shim_target_missing, file, shim + " points to " + file + ", which does not exist");
            const std::string extension = win32::extname(file);
            if (ext_in(extension, {".exe", ".com"})) return {file, {}};
            if (ext_in(extension, {".js", ".cjs", ".mjs"})) {
                if (policy == Policy::allow_node_entry) return node_entry(file, lookup);
                throw ResolutionError(ResolutionErrorKind::script, file, shim + " points to " + file + ", the Node-based CLI; a native executable is required");
            }
            throw ResolutionError(ResolutionErrorKind::unspawnable, file, shim + " points to " + file + ", which cannot be spawned without a shell");
        }
        // 4. The npm package's known JS entry (allow_node_entry only).
        if (policy == Policy::allow_node_entry) {
            std::string entry;
            if (name == "codex") entry = win32::join(dir, "node_modules\\@openai\\codex\\bin\\codex.js");
            if (name == "claude") entry = win32::join(dir, "node_modules\\@anthropic-ai\\claude-code\\cli.js");
            if (!entry.empty() && lookup.is_file(entry)) return node_entry(entry, lookup);
        }
    }
    if (ext_in(win32::extname(command), {".cmd", ".bat"}))
        throw ResolutionError(ResolutionErrorKind::unspawnable, command, "One-shot launch requires a native executable or a known npm CLI entry point");
    if (policy == Policy::allow_node_entry) return {command, {}}; // spawn reports ENOENT (TS behaviour)
    throw ResolutionError(ResolutionErrorKind::not_found, "", command + " not found on PATH");
}

ResolvedExecutable resolve_posix(const std::string& command, const ExecutableLookup& lookup, Policy policy) {
    if (policy == Policy::allow_node_entry) return {command, {}}; // spawned as given (TS behaviour)
    std::optional<std::string> found;
    if (command.find('/') != std::string::npos) {
        const std::string path = command[0] == '/' || lookup.cwd.empty() ? command : lookup.cwd + "/" + command;
        if (lookup.is_file(path)) found = path;
    } else {
        for (const auto& dir : text::split(lookup.path_list, ':')) {
            if (dir.empty()) continue;
            const std::string candidate = dir.back() == '/' ? dir + command : dir + "/" + command;
            if (lookup.is_file(candidate)) {
                found = candidate;
                break;
            }
        }
    }
    if (!found) throw ResolutionError(ResolutionErrorKind::not_found, "", command + " not found on PATH");
    const std::string head = lookup.read_head ? lookup.read_head(*found, 4) : std::string();
    if (is_native_executable_head(head)) return {*found, {}};
    if (starts_with(head, "#!"))
        throw ResolutionError(ResolutionErrorKind::script, *found, command + " resolved to " + *found + ", a script (the Node-based CLI); a native executable is required");
    throw ResolutionError(ResolutionErrorKind::not_native, *found, command + " resolved to " + *found + ", which is not a native executable");
}

} // namespace

bool is_native_executable_head(std::string_view head) noexcept {
    if (head.size() < 4) return false;
    const auto byte = [&](std::size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(head[i])); };
    if (byte(0) == 0x7f && head.substr(1, 3) == "ELF") return true;
    const unsigned magic = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
    return magic == 0xfeedfaceu || magic == 0xfeedfacfu || magic == 0xcefaedfeu || magic == 0xcffaedfeu || magic == 0xcafebabeu;
}

ResolvedExecutable resolve_executable(const std::string& command, const ExecutableLookup& lookup, Policy policy) {
    if (command.empty()) throw ResolutionError(ResolutionErrorKind::not_found, "", "no command");
    return lookup.windows ? resolve_windows(command, lookup, policy) : resolve_posix(command, lookup, policy);
}

ExecutableLookup system_lookup(const EnvList& env) {
    ExecutableLookup lookup;
#ifdef _WIN32
    lookup.windows = true;
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    lookup.arch = "arm64";
#endif
    lookup.path_list = process::env_get(env, "PATH").value_or("");
    lookup.path_ext = process::env_get(env, "PATHEXT").value_or("");
    std::error_code cwd_error;
    lookup.cwd = fs::to_utf8(std::filesystem::current_path(cwd_error));
    lookup.is_file = [](const std::string& file) {
        std::error_code error;
        return std::filesystem::is_regular_file(fs::from_utf8(file), error);
    };
    lookup.read_text = [](const std::string& file) {
        auto result = fs::read_file(fs::from_utf8(file), 1u << 20);
        return result.status == fs::ReadStatus::ok ? std::move(result.bytes) : std::string();
    };
    lookup.read_head = [](const std::string& file, std::size_t bytes) {
        std::ifstream in(fs::from_utf8(file), std::ios::binary);
        std::string head(std::min<std::size_t>(bytes, 4096), '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
        return head;
    };
    return lookup;
}

// --- launch ---------------------------------------------------------------------------------------

Launch prepare(std::string file, std::vector<std::string> args, const PrepareOptions& options) {
    // Migrate an explicit cmd.exe wrapper to the real CLI, without executing shell text.
    if (ascii_iequals(win32::basename(file), "cmd") || ascii_iequals(win32::basename(file), "cmd.exe")) {
        const auto marker = std::find_if(args.begin(), args.end(), [](const std::string& a) { return ascii_iequals(a, "/c"); });
        if (marker == args.end() || marker + 1 == args.end() || (marker + 1)->empty() ||
            (marker + 1)->find_first_of(" \t\r\n\v\f") != std::string::npos) {
            throw OneShotError("Unsupported CLI shell wrapper");
        }
        file = *(marker + 1);
        args = std::vector<std::string>(marker + 2, args.end());
    }
    const auto provider = detect_provider(args);
    if (!provider) throw OneShotError("One-shot launch requires claude print mode or codex exec");
    Launch launch;
    launch.provider = *provider;
    launch.env = subscription_environment(options.env ? *options.env : process::current_environment());
    const ExecutableLookup lookup = options.lookup ? *options.lookup : system_lookup(launch.env);
    if (*provider == Provider::claude && lookup.windows && !process::env_get(launch.env, "CLAUDE_CODE_GIT_BASH_PATH")) {
        for (const char* bash : {"C:/Program Files/Git/bin/bash.exe", "C:/Program Files (x86)/Git/bin/bash.exe"}) {
            if (lookup.is_file && lookup.is_file(bash)) {
                launch.env.emplace_back("CLAUDE_CODE_GIT_BASH_PATH", bash);
                break;
            }
        }
    }
    const auto resolved = resolve_executable(file, lookup, options.policy);
    launch.file = resolved.file;
    launch.args = resolved.prefix_args;
    const auto model_args = resolve_model_args(std::move(args), *provider, launch.env);
    launch.args.insert(launch.args.end(), model_args.begin(), model_args.end());
    return launch;
}

process::Result run(const Launch& launch, std::string_view prompt, const RunOptions& options, const std::atomic<bool>* cancel) {
    process::Spec spec;
    spec.argv.reserve(launch.args.size() + 1);
    spec.argv.push_back(launch.file);
    spec.argv.insert(spec.argv.end(), launch.args.begin(), launch.args.end());
    spec.cwd = options.cwd;
    spec.env_mode = process::EnvMode::replace;
    spec.env = launch.env;
    spec.stdin_data = std::string(prompt);
    spec.timeout = options.timeout;
    spec.max_stdout = options.max_stdout;
    spec.max_stderr = options.max_stderr;
    spec.stderr_tail_only = options.stderr_tail_only;
    spec.hide_window = true;
    return process::run(spec, cancel);
}

} // namespace lapilli::one_shot
