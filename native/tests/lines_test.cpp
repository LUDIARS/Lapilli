// lapilli::lines: line splitting compared with Node readline (lf_crlf_cr) and String#split(/\r?\n/)
// (lf_crlf), chunk boundaries anywhere, and the Lorica budget: exactly max_line, one over, a huge
// line without terminator, under each overflow policy.
#include "lapilli/lines.hpp"
#include "lapilli/test/check.hpp"

#include <sstream>

using namespace lapilli::lines;
using Texts = std::vector<std::string>;

namespace {
Texts texts(const std::vector<Line>& lines) {
    Texts out;
    for (const auto& line : lines) out.push_back(line.text);
    return out;
}
Texts split_all(const std::string& bytes, Options options = {}) { return texts(split_lines(bytes, options)); }
/** The same input fed one byte at a time must give the same lines. */
Texts bytewise(const std::string& bytes, Options options = {}) {
    LineSplitter splitter(options);
    std::vector<Line> lines;
    for (char c : bytes)
        for (auto& line : splitter.push(std::string_view(&c, 1))) lines.push_back(std::move(line));
    for (auto& line : splitter.finish()) lines.push_back(std::move(line));
    return texts(lines);
}
Texts read_all(const std::string& bytes, Options options = {}) {
    std::istringstream input(bytes);
    LineReader reader(input, options);
    Texts out;
    while (auto line = reader.next()) out.push_back(line->text);
    return out;
}
} // namespace

LAPILLI_TEST("lf_crlf_cr: LF, CR LF or a lone CR end a line; the last needs no terminator (Node readline)") {
    const std::pair<std::string, Texts> cases[] = {
        {"a\rb\nc", {"a", "b", "c"}}, {"a\r\n\r\nb\r", {"a", "", "b"}}, {"x\n", {"x"}}, {"", {}}, {"\n", {""}},
        {"a\r", {"a"}}, {"\r\n", {""}}, {"a\n\rb", {"a", "", "b"}}, {"{\"t\":1}\r\n{\"a\":1}", {"{\"t\":1}", "{\"a\":1}"}},
    };
    for (const auto& [bytes, expected] : cases) {
        LAPILLI_CHECK(split_all(bytes) == expected);
        LAPILLI_CHECK(bytewise(bytes) == expected);
        LAPILLI_CHECK(read_all(bytes) == expected);
    }
}

LAPILLI_TEST("lf_crlf: only LF ends a line; CR before LF is dropped, a lone CR stays") {
    Options options;
    options.terminators = Terminators::lf_crlf;
    const std::pair<std::string, Texts> cases[] = {
        {"a\r\nb\rc\n", {"a", "b\rc"}}, {"a\r", {"a\r"}}, {"\r\n\r\n", {"", ""}}, {"x", {"x"}},
    };
    for (const auto& [bytes, expected] : cases) {
        LAPILLI_CHECK(split_all(bytes, options) == expected);
        LAPILLI_CHECK(bytewise(bytes, options) == expected);
    }
}

LAPILLI_TEST("invalid UTF-8 becomes U+FFFD; sanitizing can be turned off") {
    LAPILLI_CHECK(split_all("a\xff\xfe" "b\n") == (Texts{"a\xEF\xBF\xBD\xEF\xBF\xBD" "b"}));
    LAPILLI_CHECK(split_all("\xC3\xA9\n") == (Texts{"\xC3\xA9"}));
    Options raw;
    raw.sanitize_utf8 = false;
    LAPILLI_CHECK(split_all("\xff\n", raw) == (Texts{"\xff"}));
}

LAPILLI_TEST("boundary: exactly max_line is read, one byte more throws; earlier lines are kept") {
    Options small;
    small.max_line = 8;
    LAPILLI_CHECK(split_all("12345678\nx\n", small) == (Texts{"12345678", "x"}));
    LAPILLI_CHECK(bytewise("12345678\r\n", small) == (Texts{"12345678"}));
    Options crlf = small;
    crlf.terminators = Terminators::lf_crlf;
    LAPILLI_CHECK(bytewise("12345678\r\n", crlf) == (Texts{"12345678"})); // the CR does not count
    LAPILLI_CHECK_THROWS_AS(split_all("123456789\nx\n", small), LineTooLong);
    // The reader returns the lines before the long one, then throws, then goes on after it.
    std::istringstream input("ok\n123456789\nafter\n");
    LineReader reader(input, small);
    LAPILLI_CHECK_EQ(reader.next()->text, std::string("ok"));
    LAPILLI_CHECK_THROWS_AS(reader.next(), LineTooLong);
    LAPILLI_CHECK_EQ(reader.next()->text, std::string("after"));
    LAPILLI_CHECK(!reader.next().has_value());
    // The splitter keeps completed lines for take_completed().
    LineSplitter splitter(small);
    LAPILLI_CHECK_THROWS_AS(splitter.push("a\nb\n123456789\nc\n"), LineTooLong);
    LAPILLI_CHECK(texts(splitter.take_completed()) == (Texts{"a", "b"}));
    LAPILLI_CHECK(texts(splitter.push("")) == (Texts{"c"}));
}

LAPILLI_TEST("boundary: the default 16 MiB limit, exact and one over; a huge unterminated line is refused") {
    const std::size_t limit = Options{}.max_line;
    const auto exact = split_lines(std::string(limit, 'x') + "\n");
    LAPILLI_CHECK_EQ(exact.size(), 1u);
    LAPILLI_CHECK_EQ(exact[0].text.size(), limit);
    LAPILLI_CHECK_THROWS_AS(split_lines(std::string(limit + 1, 'x') + "\n"), LineTooLong);
    LineSplitter splitter;
    bool threw = false;
    try {
        for (int i = 0; i < 300; ++i) splitter.push(std::string(64 * 1024, 'y'));
    } catch (const LineTooLong& error) {
        threw = true;
        LAPILLI_CHECK_EQ(error.limit(), limit);
    }
    LAPILLI_CHECK(threw);
    LAPILLI_CHECK(splitter.buffered() <= limit);
}

LAPILLI_TEST("overflow policies truncate_emit and discard") {
    Options truncate;
    truncate.max_line = 4;
    truncate.overflow = Overflow::truncate_emit;
    const auto lines = split_lines("abcdefg\nhi\n", truncate);
    LAPILLI_CHECK(lines == (std::vector<Line>{{"abcd", true}, {"hi", false}}));
    LAPILLI_CHECK(bytewise("abcdefg\nhi", truncate) == (Texts{"abcd", "hi"}));
    Options discard = truncate;
    discard.overflow = Overflow::discard;
    LineSplitter splitter(discard);
    auto kept = splitter.push("abcdefg\nhi\nlonger!\n");
    LAPILLI_CHECK(texts(kept) == (Texts{"hi"}));
    LAPILLI_CHECK_EQ(splitter.discarded(), 2u);
}
