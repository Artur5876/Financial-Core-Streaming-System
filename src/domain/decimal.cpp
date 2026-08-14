#include "domain/decimal.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace fincore::domain {
namespace {

[[nodiscard]] std::uint64_t checked_accumulate(std::uint64_t current, int digit) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (current > (max - static_cast<std::uint64_t>(digit)) / 10U) {
        throw std::out_of_range("decimal value exceeds the supported range");
    }
    return current * 10U + static_cast<std::uint64_t>(digit);
}

} // namespace

Decimal Decimal::from_string(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("decimal string is empty");
    }

    bool negative = false;
    std::size_t position = 0;
    if (value.front() == '+' || value.front() == '-') {
        negative = value.front() == '-';
        position = 1;
    }
    if (position == value.size()) {
        throw std::invalid_argument("decimal has a sign but no digits");
    }

    std::uint64_t whole = 0;
    bool saw_whole_digit = false;
    while (position < value.size() && value[position] != '.') {
        const char ch = value[position];
        if (ch < '0' || ch > '9') {
            throw std::invalid_argument("decimal contains a non-digit character");
        }
        whole = checked_accumulate(whole, ch - '0');
        saw_whole_digit = true;
        ++position;
    }
    if (!saw_whole_digit) {
        throw std::invalid_argument("decimal requires digits before the decimal point");
    }

    std::uint64_t fraction = 0;
    int fraction_digits = 0;
    if (position < value.size()) {
        ++position;
        if (position == value.size()) {
            throw std::invalid_argument("decimal point must be followed by digits");
        }
        while (position < value.size()) {
            const char ch = value[position];
            if (ch < '0' || ch > '9') {
                throw std::invalid_argument("decimal contains a non-digit character");
            }
            if (fraction_digits == 8) {
                throw std::invalid_argument("decimal contains more than eight fractional digits");
            }
            fraction = fraction * 10U + static_cast<std::uint64_t>(ch - '0');
            ++fraction_digits;
            ++position;
        }
    }
    while (fraction_digits < 8) {
        fraction *= 10U;
        ++fraction_digits;
    }

    constexpr auto positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr auto negative_limit = positive_limit + 1U;
    const std::uint64_t limit = negative ? negative_limit : positive_limit;
    const auto scale_value = static_cast<std::uint64_t>(scale);
    if (whole > (limit - fraction) / scale_value) {
        throw std::out_of_range("decimal value exceeds int64 range");
    }

    const std::uint64_t magnitude = whole * scale_value + fraction;
    if (!negative) {
        return from_raw(static_cast<std::int64_t>(magnitude));
    }
    if (magnitude == negative_limit) {
        return from_raw(std::numeric_limits<std::int64_t>::min());
    }
    return from_raw(-static_cast<std::int64_t>(magnitude));
}

std::string Decimal::to_string() const {
    const bool negative = raw_ < 0;
    const std::uint64_t magnitude = negative
        ? static_cast<std::uint64_t>(-(raw_ + 1)) + 1U
        : static_cast<std::uint64_t>(raw_);
    const auto scale_value = static_cast<std::uint64_t>(scale);
    const std::uint64_t whole = magnitude / scale_value;
    std::uint64_t fraction = magnitude % scale_value;

    std::string result = negative ? "-" : "";
    result += std::to_string(whole);
    result.push_back('.');

    std::string fraction_text = std::to_string(fraction);
    result.append(8U - fraction_text.size(), '0');
    result += fraction_text;
    return result;
}

} // namespace fincore::domain
