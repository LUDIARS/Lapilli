// Lorica budgets for external input. An input is charged (bytes, elements, allocations, nesting
// depth) before it is allocated or expanded; going over the budget throws BudgetExceeded.
#pragma once

#include <lorica/checked_size.hpp>
#include <lorica/input_budget.hpp>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace lapilli {

class BudgetExceeded : public std::runtime_error {
public:
    explicit BudgetExceeded(const std::string& what) : std::runtime_error(what) {}
};

/** A limit that never binds (for the dimensions an input does not use). */
inline constexpr std::size_t unlimited = std::numeric_limits<std::size_t>::max();

/** Byte-only limits (elements / allocations unbounded, depth 0). */
constexpr lorica::budget_limits byte_limits(std::size_t bytes) noexcept { return {bytes, unlimited, unlimited, 0}; }

/** One input's budget; charge() throws BudgetExceeded instead of returning false. */
class Budget {
public:
    Budget(lorica::budget_limits limits, std::string what) : budget_(limits), what_(std::move(what)) {}

    void charge(std::size_t bytes, std::size_t elements = 0, std::size_t allocations = 0) {
        if (!budget_.consume(bytes, elements, allocations)) throw BudgetExceeded(what_ + ": input exceeds its budget");
    }
    void depth(std::size_t depth) const {
        if (!budget_.permits_depth(depth)) throw BudgetExceeded(what_ + ": input nests deeper than its budget");
    }
    [[nodiscard]] lorica::budget_limits remaining() const noexcept { return budget_.remaining(); }
    [[nodiscard]] lorica::input_budget& raw() noexcept { return budget_; }
    [[nodiscard]] const std::string& what() const noexcept { return what_; }

private:
    lorica::input_budget budget_;
    std::string what_;
};

/** size_t arithmetic that throws BudgetExceeded on overflow (lorica::checked_*). */
inline std::size_t checked_add(std::size_t a, std::size_t b) {
    auto sum = lorica::checked_add(a, b);
    if (!sum) throw BudgetExceeded("size overflow");
    return *sum;
}
inline std::size_t checked_multiply(std::size_t a, std::size_t b) {
    auto product = lorica::checked_multiply(a, b);
    if (!product) throw BudgetExceeded("size overflow");
    return *product;
}

} // namespace lapilli
