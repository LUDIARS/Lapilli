#include "lapilli/cli.hpp"

namespace lapilli::cli {

namespace {

const std::pair<std::string, ArgOption>* find_long(const ArgOptions& options, const std::string& name) {
    for (const auto& entry : options) if (entry.first == name) return &entry;
    return nullptr;
}

const std::pair<std::string, ArgOption>* find_short(const ArgOptions& options, char c) {
    for (const auto& entry : options) if (entry.second.short_name == c) return &entry;
    return nullptr;
}

std::string short_and_long(const std::pair<std::string, ArgOption>& entry) {
    return entry.second.short_name ? std::string("-") + *entry.second.short_name + ", --" + entry.first : "--" + entry.first;
}

bool option_like(const std::string& value) { return value.size() > 1 && value[0] == '-'; }

[[noreturn]] void unknown(const std::string& raw, bool allow_positionals) {
    if (!allow_positionals) throw ParseArgsError("Unknown option '" + raw + "'");
    throw ParseArgsError("Unknown option '" + raw + "'. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- \"" + raw + "\"");
}

void store(ParsedArgs& out, const std::pair<std::string, ArgOption>& entry, const std::optional<std::string>& value, const std::string& raw, bool from_short) {
    const auto& [name, spec] = entry;
    if (spec.type == ArgOption::Type::boolean) {
        if (value) throw ParseArgsError("Option '" + short_and_long(entry) + "' does not take an argument");
        out.booleans[name] = true;
        return;
    }
    if (!value) throw ParseArgsError("Option '" + short_and_long(entry) + " <value>' argument missing");
    (void)raw;
    (void)from_short;
    auto& values = out.strings[name];
    if (spec.multiple) values.push_back(*value);
    else values = {*value};
}

void check_ambiguous(const std::pair<std::string, ArgOption>& entry, const std::string& value, const std::string& raw, bool from_short) {
    if (!option_like(value)) return;
    std::string hint = "'--" + entry.first + "=-XYZ'";
    if (from_short) hint += " or '" + raw + "-XYZ'";
    throw ParseArgsError("Option '" + raw + "' argument is ambiguous.\nDid you forget to specify the option argument for '" + raw +
                         "'?\nTo specify an option argument starting with a dash use " + hint + ".");
}

} // namespace

ParsedArgs parse_args(const std::vector<std::string>& args, const ArgOptions& options, bool allow_positionals) {
    ParsedArgs out;
    auto positional = [&](const std::string& value) {
        if (!allow_positionals) throw ParseArgsError("Unexpected argument '" + value + "'. This command does not take positional arguments");
        out.positionals.push_back(value);
    };
    // Expand short option groups first, as Node does (-ab -> -a -b; a string option takes the rest).
    std::vector<std::string> expanded;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            for (; i < args.size(); ++i) expanded.push_back(args[i]);
            break;
        }
        bool short_group = arg.size() > 2 && arg[0] == '-' && arg[1] != '-';
        if (short_group) {
            const auto* first = find_short(options, arg[1]);
            if (first != nullptr && first->second.type == ArgOption::Type::string) {
                expanded.push_back(arg); // -s3: short option and value
                continue;
            }
            for (std::size_t at = 1; at < arg.size(); ++at) {
                const auto* entry = find_short(options, arg[at]);
                if (entry != nullptr && entry->second.type == ArgOption::Type::string && at + 1 < arg.size()) {
                    expanded.push_back(std::string("-") + arg[at] + arg.substr(at + 1));
                    break;
                }
                expanded.push_back(std::string("-") + arg[at]);
            }
            continue;
        }
        expanded.push_back(arg);
    }
    for (std::size_t i = 0; i < expanded.size(); ++i) {
        const std::string& arg = expanded[i];
        if (arg == "--") {
            for (++i; i < expanded.size(); ++i) positional(expanded[i]);
            break;
        }
        if (arg.size() == 2 && arg[0] == '-' && arg[1] != '-') {
            const auto* entry = find_short(options, arg[1]);
            if (entry == nullptr) unknown(arg, allow_positionals);
            std::optional<std::string> value;
            if (entry->second.type == ArgOption::Type::string && i + 1 < expanded.size()) {
                value = expanded[++i];
                check_ambiguous(*entry, *value, arg, true);
            }
            store(out, *entry, value, arg, true);
            continue;
        }
        if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') {
            // -s3 (a string short option with its value)
            const auto* entry = find_short(options, arg[1]);
            if (entry == nullptr) unknown(arg.substr(0, 2), allow_positionals);
            store(out, *entry, arg.substr(2), arg.substr(0, 2), true);
            continue;
        }
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            auto eq = arg.find('=');
            std::string raw = eq == std::string::npos ? arg : arg.substr(0, eq);
            const auto* entry = find_long(options, raw.substr(2));
            if (entry == nullptr) unknown(raw, allow_positionals);
            std::optional<std::string> value;
            if (eq != std::string::npos) {
                value = arg.substr(eq + 1);
            } else if (entry->second.type == ArgOption::Type::string && i + 1 < expanded.size()) {
                value = expanded[++i];
                check_ambiguous(*entry, *value, raw, false);
            }
            store(out, *entry, value, raw, false);
            continue;
        }
        positional(arg);
    }
    for (const auto& [name, spec] : options) {
        if (spec.type == ArgOption::Type::boolean) {
            if (out.booleans.count(name) == 0 && spec.default_boolean) out.booleans[name] = *spec.default_boolean;
        } else if (out.strings.count(name) == 0 && spec.default_value) {
            out.strings[name] = *spec.default_value;
        }
    }
    return out;
}

} // namespace lapilli::cli
