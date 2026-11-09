// lapilli::cli — node:util parseArgs (strict mode) as the CLI commands use it: string / boolean options,
// `multiple`, `short`, `default`, allowPositionals, `--` ending options. Errors throw
// ParseArgsError with Node's message text (the CLI prints it as a usage error).
#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lapilli::cli {

struct ArgOption {
    enum class Type { string, boolean };
    Type type = Type::string;
    bool multiple = false;
    std::optional<char> short_name;
    /** default (string options: one value; multiple: the list). */
    std::optional<std::vector<std::string>> default_value;
    std::optional<bool> default_boolean;
};

/** Option name -> spec, in declaration order (Node's values keep the order options are met). */
using ArgOptions = std::vector<std::pair<std::string, ArgOption>>;

struct ParsedArgs {
    /** string options: one or (multiple) many values. */
    std::map<std::string, std::vector<std::string>> strings;
    std::map<std::string, bool> booleans;
    std::vector<std::string> positionals;

    [[nodiscard]] std::optional<std::string> string(const std::string& name) const {
        auto it = strings.find(name);
        if (it == strings.end() || it->second.empty()) return std::nullopt;
        return it->second.back();
    }
    [[nodiscard]] std::vector<std::string> all(const std::string& name) const {
        auto it = strings.find(name);
        return it == strings.end() ? std::vector<std::string>{} : it->second;
    }
    [[nodiscard]] bool flag(const std::string& name) const {
        auto it = booleans.find(name);
        return it != booleans.end() && it->second;
    }
    [[nodiscard]] bool has(const std::string& name) const { return strings.count(name) > 0 || booleans.count(name) > 0; }
};

class ParseArgsError : public std::runtime_error {
public:
    explicit ParseArgsError(const std::string& what) : std::runtime_error(what) {}
};

ParsedArgs parse_args(const std::vector<std::string>& args, const ArgOptions& options, bool allow_positionals);

} // namespace lapilli::cli
