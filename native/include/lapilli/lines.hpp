// Splitting a byte stream into text lines, with every line charged to a Lorica budget before it is
// stored. Used for JSON Lines protocols on stdio, child process output and line-based files.
//
//   terminators lf_crlf     a line ends at LF; a CR right before the LF is dropped
//                           (String#split(/\r?\n/), most JSONL readers);
//               lf_crlf_cr  LF, CR LF or a lone CR (Node readline with crlfDelay Infinity).
//   The last line without a terminator is still a line (finish()); an empty input has none.
//   max_line    bytes of one line (terminator excluded): exactly max_line is accepted.
//   overflow    throw_error    push() throws LineTooLong when the line passes max_line. Lines
//                              completed before it in the same chunk are not lost: take them with
//                              take_completed() (or they come first from the next push()). The rest
//                              of the long line is skipped if the caller keeps feeding;
//               truncate_emit  the first max_line bytes are emitted with truncated = true;
//               discard        the line is dropped silently (counted in discarded()).
//   sanitize_utf8  invalid UTF-8 becomes U+FFFD (as Node's utf8 decoder makes it).
#pragma once

#include "lapilli/budget.hpp"

#include <cstddef>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::lines {

enum class Overflow { throw_error, truncate_emit, discard };
enum class Terminators { lf_crlf, lf_crlf_cr };

struct Options {
    std::size_t max_line = 16u << 20;
    Overflow overflow = Overflow::throw_error;
    Terminators terminators = Terminators::lf_crlf_cr;
    bool sanitize_utf8 = true;
};

struct Line {
    std::string text;
    bool truncated = false;
    bool operator==(const Line&) const = default;
};

class LineTooLong : public BudgetExceeded {
public:
    explicit LineTooLong(std::size_t limit)
        : BudgetExceeded("a line is longer than " + std::to_string(limit) + " bytes"), limit_(limit) {}
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

private:
    std::size_t limit_;
};

class LineSplitter {
public:
    explicit LineSplitter(Options options = {});

    /** Feeds a chunk; returns the lines it completed. Throws LineTooLong (throw_error policy). */
    std::vector<Line> push(std::string_view chunk);
    /** The input ended: the lines still pending (the unterminated last line, if any). May throw LineTooLong. */
    std::vector<Line> finish();
    /** After a LineTooLong: the lines completed before the long one (otherwise returned by the next push()). */
    std::vector<Line> take_completed();
    /** Bytes of the line being collected. */
    [[nodiscard]] std::size_t buffered() const noexcept { return current_.size(); }
    /** Lines dropped by the discard policy. */
    [[nodiscard]] std::size_t discarded() const noexcept { return discarded_; }

private:
    /** Stores bytes of the current line; true when this call overflowed under throw_error. */
    bool append(std::string_view bytes);
    std::optional<Line> end_line();
    /** Splits `data` into `out`; returns where it stopped on a throw_error overflow. */
    std::optional<std::size_t> process(std::string_view data, std::vector<Line>& out);

    Options options_;
    std::string current_;
    std::vector<Line> carry_;
    std::string rest_;
    bool pending_cr_ = false;
    lorica::input_budget budget_;
    bool any_ = false;
    bool over_ = false;
    bool skip_lf_ = false;
    std::size_t discarded_ = 0;
};

/** Lines of an istream, read as they arrive (does not wait for more than is available). */
class LineReader {
public:
    LineReader(std::istream& input, Options options = {});
    /** The next line, or nullopt at the end of the input. Throws LineTooLong (throw_error policy). */
    std::optional<Line> next();

private:
    std::istream& input_;
    LineSplitter splitter_;
    std::vector<Line> ready_;
    std::size_t at_ = 0;
    bool ended_ = false;
    std::optional<std::size_t> pending_error_;
};

/** Splits a whole text at once (convenience over LineSplitter). */
std::vector<Line> split_lines(std::string_view text, Options options = {});

} // namespace lapilli::lines
