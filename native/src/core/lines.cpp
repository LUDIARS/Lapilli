#include "lapilli/lines.hpp"

#include "lapilli/text.hpp"

#include <algorithm>
#include <array>

namespace lapilli::lines {

LineSplitter::LineSplitter(Options options) : options_(options), budget_(byte_limits(options.max_line)) {}

bool LineSplitter::append(std::string_view bytes) {
    any_ = true;
    if (bytes.empty() || over_) return false; // past the limit: the rest of the line is not stored
    if (budget_.consume(bytes.size(), 0, 0)) {
        current_.append(bytes);
        return false;
    }
    over_ = true;
    if (options_.overflow == Overflow::truncate_emit) {
        const std::size_t room = budget_.remaining().bytes;
        (void)budget_.consume(room, 0, 0);
        current_.append(bytes.substr(0, room));
        return false;
    }
    current_.clear();
    return options_.overflow == Overflow::throw_error;
}

std::optional<Line> LineSplitter::end_line() {
    const bool was_over = over_;
    Line line{std::move(current_), was_over};
    current_.clear();
    budget_ = lorica::input_budget(byte_limits(options_.max_line));
    any_ = false;
    over_ = false;
    if (was_over && options_.overflow != Overflow::truncate_emit) {
        if (options_.overflow == Overflow::discard) ++discarded_;
        return std::nullopt;
    }
    if (options_.sanitize_utf8 && !text::is_valid_utf8(line.text)) line.text = text::sanitize_utf8(line.text);
    return line;
}

std::optional<std::size_t> LineSplitter::process(std::string_view data, std::vector<Line>& out) {
    const bool cr_ends = options_.terminators == Terminators::lf_crlf_cr;
    std::size_t at = 0;
    if (data.empty()) return std::nullopt;
    if (skip_lf_) {
        skip_lf_ = false;
        if (data[0] == '\n') at = 1;
    }
    if (pending_cr_) {
        // lf_crlf: a CR that ended the previous chunk is dropped when an LF follows it.
        pending_cr_ = false;
        if (data[0] != '\n' && append("\r")) return std::size_t{0};
    }
    while (at < data.size()) {
        const auto end = cr_ends ? data.find_first_of("\r\n", at) : data.find('\n', at);
        if (end == std::string_view::npos) {
            std::string_view piece = data.substr(at);
            if (!cr_ends && piece.back() == '\r') {
                piece.remove_suffix(1);
                pending_cr_ = true;
            }
            if (append(piece)) return data.size();
            if (pending_cr_) any_ = true;
            break;
        }
        std::string_view piece = data.substr(at, end - at);
        if (!cr_ends && !piece.empty() && piece.back() == '\r') piece.remove_suffix(1);
        const bool overflowed = append(piece);
        any_ = true;
        at = end + 1;
        if (cr_ends && data[end] == '\r') {
            if (at < data.size()) {
                if (data[at] == '\n') ++at;
            } else {
                skip_lf_ = true;
            }
        }
        if (overflowed) {
            end_line(); // the refused line is dropped; its terminator was consumed
            return at;
        }
        if (auto line = end_line()) out.push_back(std::move(*line));
    }
    return std::nullopt;
}

std::vector<Line> LineSplitter::push(std::string_view chunk) {
    std::vector<Line> out = std::move(carry_);
    carry_.clear();
    std::string joined;
    std::string_view data = chunk;
    if (!rest_.empty()) {
        joined = std::move(rest_);
        rest_.clear();
        joined.append(chunk);
        data = joined;
    }
    if (const auto stop = process(data, out)) {
        carry_ = std::move(out);
        rest_ = std::string(data.substr(*stop));
        throw LineTooLong(options_.max_line);
    }
    return out;
}

std::vector<Line> LineSplitter::finish() {
    std::vector<Line> out = push({});
    skip_lf_ = false;
    if (pending_cr_) {
        // "a\r" at the end of an lf_crlf stream keeps its CR (String#split(/\r?\n/)).
        pending_cr_ = false;
        if (append("\r")) {
            carry_ = std::move(out);
            end_line();
            throw LineTooLong(options_.max_line);
        }
    }
    // A line refused earlier (already reported by push) is dropped here.
    if (any_)
        if (auto line = end_line()) out.push_back(std::move(*line));
    return out;
}

std::vector<Line> LineSplitter::take_completed() {
    std::vector<Line> out = std::move(carry_);
    carry_.clear();
    return out;
}

LineReader::LineReader(std::istream& input, Options options) : input_(input), splitter_(options) {}

std::optional<Line> LineReader::next() {
    for (;;) {
        if (at_ < ready_.size()) return std::move(ready_[at_++]);
        ready_.clear();
        at_ = 0;
        if (pending_error_) {
            const std::size_t limit = *pending_error_;
            pending_error_.reset();
            throw LineTooLong(limit);
        }
        if (ended_) return std::nullopt;
        try {
            std::streambuf* buffer = input_.rdbuf();
            if (buffer == nullptr) {
                ended_ = true;
                ready_ = splitter_.finish();
                continue;
            }
            // Take what is already buffered; otherwise wait for one byte (never for a full block,
            // so an interactive pipe yields each line as soon as it arrives).
            std::array<char, 65536> chunk{};
            std::streamsize got = 0;
            const std::streamsize available = buffer->in_avail();
            if (available > 0) {
                got = buffer->sgetn(chunk.data(), std::min<std::streamsize>(available, static_cast<std::streamsize>(chunk.size())));
            } else {
                const auto c = buffer->sbumpc();
                if (std::char_traits<char>::eq_int_type(c, std::char_traits<char>::eof())) {
                    ended_ = true;
                    input_.setstate(std::ios::eofbit);
                    ready_ = splitter_.finish();
                    continue;
                }
                chunk[0] = std::char_traits<char>::to_char_type(c);
                got = 1;
            }
            ready_ = splitter_.push(std::string_view(chunk.data(), static_cast<std::size_t>(got)));
        } catch (const LineTooLong& error) {
            // Lines completed before the long one come first, then the error.
            ready_ = splitter_.take_completed();
            pending_error_ = error.limit();
        }
    }
}

std::vector<Line> split_lines(std::string_view text, Options options) {
    LineSplitter splitter(options);
    auto lines = splitter.push(text);
    for (auto& line : splitter.finish()) lines.push_back(std::move(line));
    return lines;
}

} // namespace lapilli::lines
