// lapilli::cli: node:util parseArgs (strict): values and error messages recorded from Node.
#include "lapilli/cli.hpp"
#include "lapilli/test/check.hpp"

using lapilli::cli::ArgOption;

namespace {
lapilli::cli::ArgOptions opts() {
    ArgOption game;
    ArgOption json{ArgOption::Type::boolean, false, 'j', std::nullopt, std::nullopt};
    ArgOption runs{ArgOption::Type::string, true, std::nullopt, std::nullopt, std::nullopt};
    ArgOption seed{ArgOption::Type::string, false, 's', std::nullopt, std::nullopt};
    ArgOption verbose{ArgOption::Type::boolean, false, 'v', std::nullopt, std::nullopt};
    ArgOption level{ArgOption::Type::string, false, std::nullopt, std::vector<std::string>{"info"}, std::nullopt};
    ArgOption color{ArgOption::Type::boolean, false, std::nullopt, std::nullopt, true};
    return {{"game", game}, {"json", json}, {"runs", runs}, {"seed", seed}, {"verbose", verbose}, {"level", level}, {"color", color}};
}
std::string error_of(const std::vector<std::string>& args, bool allow = true) {
    try {
        lapilli::cli::parse_args(args, opts(), allow);
    } catch (const lapilli::cli::ParseArgsError& e) {
        return e.what();
    }
    return "";
}
} // namespace

LAPILLI_TEST("values, multiple, short groups, defaults and -- are parsed as Node does") {
    using lapilli::cli::parse_args;
    auto a = parse_args({"--game=a", "--runs", "a", "--runs=b", "-s", "3", "--", "--json", "p"}, opts(), true);
    LAPILLI_CHECK_EQ(*a.string("game"), std::string("a"));
    LAPILLI_CHECK(a.all("runs") == (std::vector<std::string>{"a", "b"}));
    LAPILLI_CHECK_EQ(*a.string("seed"), std::string("3"));
    LAPILLI_CHECK(a.positionals == (std::vector<std::string>{"--json", "p"}));
    LAPILLI_CHECK_EQ(*a.string("level"), std::string("info"));
    LAPILLI_CHECK(a.flag("color"));
    auto b = parse_args({"-jv"}, opts(), true);
    LAPILLI_CHECK(b.flag("json") && b.flag("verbose"));
    auto c = parse_args({"-js", "3"}, opts(), true);
    LAPILLI_CHECK(c.flag("json") && *c.string("seed") == "3");
    LAPILLI_CHECK_EQ(*parse_args({"-sj"}, opts(), true).string("seed"), std::string("j"));
    LAPILLI_CHECK_EQ(*parse_args({"-s3"}, opts(), true).string("seed"), std::string("3"));
    LAPILLI_CHECK_EQ(*parse_args({"--game="}, opts(), true).string("game"), std::string(""));
    LAPILLI_CHECK_EQ(*parse_args({"--game", "-"}, opts(), true).string("game"), std::string("-"));
    LAPILLI_CHECK(parse_args({"-"}, opts(), true).positionals == (std::vector<std::string>{"-"}));
    LAPILLI_CHECK_EQ(*parse_args({"--game", "a", "--game", "b"}, opts(), true).string("game"), std::string("b"));
}

LAPILLI_TEST("errors carry Node's messages") {
    LAPILLI_CHECK_EQ(error_of({"--foo"}), std::string("Unknown option '--foo'. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- \"--foo\""));
    LAPILLI_CHECK_EQ(error_of({"--foo=1"}, false), std::string("Unknown option '--foo'"));
    LAPILLI_CHECK_EQ(error_of({"--game"}), std::string("Option '--game <value>' argument missing"));
    LAPILLI_CHECK_EQ(error_of({"-s"}), std::string("Option '-s, --seed <value>' argument missing"));
    LAPILLI_CHECK_EQ(error_of({"--json=1"}), std::string("Option '-j, --json' does not take an argument"));
    LAPILLI_CHECK_EQ(error_of({"x"}, false), std::string("Unexpected argument 'x'. This command does not take positional arguments"));
    LAPILLI_CHECK_EQ(error_of({"--game", "--json"}), std::string("Option '--game' argument is ambiguous.\nDid you forget to specify the option argument for '--game'?\nTo specify an option argument starting with a dash use '--game=-XYZ'."));
    LAPILLI_CHECK_EQ(error_of({"-s", "-1"}), std::string("Option '-s' argument is ambiguous.\nDid you forget to specify the option argument for '-s'?\nTo specify an option argument starting with a dash use '--seed=-XYZ' or '-s-XYZ'."));
    LAPILLI_CHECK_EQ(error_of({"-xj"}), std::string("Unknown option '-x'. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- \"-x\""));
    LAPILLI_CHECK_EQ(error_of({"-j=1"}), std::string("Unknown option '-='. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- \"-=\""));
}
