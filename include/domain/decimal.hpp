#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace fincore::domain {

class Decimal final {
public:
    //THe scale will determine the actual number of fractional digits;
    //8 fractional digits
    static constexpr std::int64_t scale = 100'000'000;

    constexpr Decimal() noexcept = default;

    //'from_raw' function will produce already scaled integer
    //132`000`000 will be 1.2500000
    //that function will be usefull for reading already scaled integer
    //for cases such as:
    //--- implementing arithmetic internally
    //--- and implementing decoding of binary protocols(that uses the same scale(8 fraction));
    [[nodiscard]] static constexpr Decimal from_raw(
            std::int64_t raw
    )   noexcept {
        return Decimal(raw);
    }

    [[nodiscard]] static Decimal from_string(std::string_view value);

    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] constexpr std::int64_t raw() const noexcept {
        return raw_;
    }

    //C++20 feacha (three way comparison operator)
    auto operator<=>(const Decimal&) const = default;

private:
        explicit constexpr Decimal(std::int64_t raw) noexcept
            : raw_(raw) {}

        std::int64_t raw_{};
};

} // namespace fincore::domain
