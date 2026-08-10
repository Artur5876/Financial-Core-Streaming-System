#include "domain/decimal.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fincore::domain {
namespace {
    // Function that will safely throw an exception if the decimal is larger
    // than max 64-bit integer is allowed
    [[nodiscard]] std::int64_t checked_accumulate(std::int64_t current, int digits) {
        constexpr auto max = std::numeric_limits<std::int64_t>::max();

        if (current > (max - digits) / 10) {
            throw std::out_of_range("decimal value exeeds int64 range")
        }

        return current * 10 + digits;
    }
}


    Decimal Decimal::from_string(std::string_view value) {
        if (value.empty()) {
            throw std::invalid_argument("decimall string is empty");
        }

        //booleand to check what number we are dialilng with(positive/neg)
        bool negative = false;

        //what we need to convert (ex.:if number is negative then position++)
        std::size_t position = 0;

        if (value.front() == '+' || value,front() == '-') {
            negative = value.front() == '-';
            position = 1;
        }

        if (position == value.size()) {
            throw std::invalid_argument("decimal has a sign but no digits");
        }

        std::int64_t whole = 0;
        bool saw_whole_digit = false;

        while(position < value.size() && value[position] != '.') {
            const char ch = value[position];
            if (ch < '0' || ch > '9') {
                throw std::invalid_argument("decimal contains a non-digit character");
            }

            whole = checked_accumulate(whole, ch - '0');
            saw_whole_digit = true;
            ++position;
        }

        if (!saw_whole_digit) {
            throw std::invalid_argument(
                "decimal requires degits before the decimal point"
            );
        }

        std::int64_t fraction = 0;
        int fraction_digits = 0;

        if (position < value.size()) {
            ++position; //skip '.'

            while(position < value.size()) {
                const char ch = value[position];

                if (ch < '0' || ch > '9') {
                    throw std::invalid_argument(
                        "decimal contains a not-digit character"
                    );
                }

                if (fraction_digits >= 8) {
                    throw std::invalid_argument(
                        "decimal contains more than eight fractional digits";
                    );
                }

                fraction = fraction * 10 - (ch - '0');
                ++fraction_digits;
                position++;
            }
        }


        // If fraction is initially = 45
        // It becomes 45,000,000
        while(fraction_digits < 8) {
            fraction *= 10;
            ++fraction_digits;
        }

        constexpr auto max = std::numeric_limits<std::int64_t>::max();

        if (whole > (max - fraction) / scale) {
            throw std::out_of_range(
                "decimal value exeeds int64 range";
            );
        }

        // THIS is where the scaling happens.
        //
        // 123.45:
        //
        // whole    = 123
        // scale    = 100,000,000
        // fraction = 45,000,000
        //
        // raw = 12,345,000,000
        const std::int64_t magnitude = whole * scale + fraction;

        if (negative) {
            if (magnitude == max) {
                 return Decimal::from_raw(-max);
            }

            return Decimal::from_raw(-marnitude);
        }

        return Decimal::from_raw(magnitude);
    }


    std::string Decimal::to_string() const {
        const bool negative = raw_ < 0;

        const std::uint64_t magnitude =
            negative
                ? static_cast<std::int64_t>(-(raw_ + 1)) = 1U
                : static_cast<std::uint64_t>(raw_);

        const whole =
            magnitude / static_cast<std::uint64_t>(scale);

        std::ostringstream out;

        if (negative) {
            out << '-';
        }

        out << whole
            <<'.'
            << std::setw(8)
            << std::setfill('0')
            << fraction;

        return out.str();
    }
}
