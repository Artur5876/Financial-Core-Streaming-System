#include "domain/decimal.hpp"
#include "domain/validation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace fincore::domain {
namespace {

TEST(DecimalTest, ParsesAndFormatsEightDigitFixedPointValues) {
    EXPECT_EQ(Decimal::from_string("123.45").raw(), 12'345'000'000);
    EXPECT_EQ(Decimal::from_string("-0.00000001").raw(), -1);
    EXPECT_EQ(Decimal::from_string("+7").to_string(), "7.00000000");
}

TEST(DecimalTest, SupportsEntireInt64RawRange) {
    const auto minimum = Decimal::from_string("-92233720368.54775808");
    const auto maximum = Decimal::from_string("92233720368.54775807");

    EXPECT_EQ(minimum.raw(), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(maximum.raw(), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(minimum.to_string(), "-92233720368.54775808");
    EXPECT_EQ(maximum.to_string(), "92233720368.54775807");
}

TEST(DecimalTest, RejectsMalformedAndOutOfRangeInput) {
    EXPECT_THROW((void)Decimal::from_string(""), std::invalid_argument);
    EXPECT_THROW((void)Decimal::from_string(".5"), std::invalid_argument);
    EXPECT_THROW((void)Decimal::from_string("1."), std::invalid_argument);
    EXPECT_THROW((void)Decimal::from_string("1.123456789"), std::invalid_argument);
    EXPECT_THROW((void)Decimal::from_string("92233720368.54775808"), std::out_of_range);
}

TEST(ValidationTest, AcceptsAValidQuote) {
    Quote quote{
        .symbol = "AAPL",
        .source = "ALPHA_VANTAGE",
        .timestamp = {},
        .price = Decimal::from_string("201.25"),
        .open = Decimal::from_string("200"),
        .high = Decimal::from_string("202"),
        .low = Decimal::from_string("199.50"),
        .volume = Decimal::from_string("1000"),
        .change_pct = Decimal::from_string("0.625"),
    };

    EXPECT_NO_THROW(validate(quote));
}

TEST(ValidationTest, RejectsCrossedBookAndInvalidIdentifiers) {
    OrderBookSnapshot snapshot{
        .symbol = "aapl",
        .source = "STREAM",
        .snapshot_time = {},
        .best_bid = Decimal::from_string("102"),
        .best_ask = Decimal::from_string("101"),
        .imbalance = Decimal::from_string("0"),
        .total_bid_volume = Decimal::from_string("1"),
        .total_ask_volume = Decimal::from_string("1"),
    };

    EXPECT_THROW(validate(snapshot), std::invalid_argument);
    snapshot.symbol = "AAPL";
    EXPECT_THROW(validate(snapshot), std::invalid_argument);
}

} // namespace
} // namespace fincore::domain
