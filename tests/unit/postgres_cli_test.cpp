#include "cli/postgres_cli.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <sstream>
#include <vector>

namespace fincore::cli {
namespace {

class FakeRepository final : public persistence::MarketDataRepository {
public:
    void upsert_instrument(const domain::Instrument& value) override { instrument = value; }
    void upsert_data_source(const domain::DataSource& value) override { source = value; }
    persistence::InsertSummary insert_quotes(std::span<const domain::Quote> values) override {
        quote = values.front(); return {values.size(), values.size()};
    }
    persistence::InsertSummary insert_ticks(std::span<const domain::Tick> values) override {
        tick = values.front(); return {values.size(), values.size()};
    }
    persistence::InsertSummary insert_order_book_snapshots(
        std::span<const domain::OrderBookSnapshot> values) override {
        snapshot = values.front(); return {values.size(), values.size()};
    }
    persistence::InsertSummary insert_technical_indicators(
        std::span<const domain::TechnicalIndicator> values) override {
        indicator = values.front(); return {values.size(), values.size()};
    }
    std::optional<domain::Quote> latest_quote(std::string_view symbol,
                                              std::string_view source_code) override {
        latest_symbol = symbol; latest_source = source_code; return latest;
    }

    std::optional<domain::Instrument> instrument;
    std::optional<domain::DataSource> source;
    std::optional<domain::Quote> quote;
    std::optional<domain::Tick> tick;
    std::optional<domain::OrderBookSnapshot> snapshot;
    std::optional<domain::TechnicalIndicator> indicator;
    std::optional<domain::Quote> latest;
    std::string latest_symbol;
    std::string latest_source;
};

TEST(PostgresCliTest, UpsertsNormalizedInstrument) {
    FakeRepository repository;
    std::ostringstream out, err;
    PostgresCli cli{repository, out, err};
    EXPECT_EQ(cli.run({"instrument", "aapl", "Apple Inc", "equity", "NASDAQ", "4", "true"}), 0);
    ASSERT_TRUE(repository.instrument);
    EXPECT_EQ(repository.instrument->symbol, "AAPL");
    EXPECT_EQ(repository.instrument->asset_class, domain::AssetClass::equity);
    EXPECT_EQ(repository.instrument->tick_size_decimals, 4);
    EXPECT_TRUE(repository.instrument->is_active);
    EXPECT_TRUE(err.str().empty());
}

TEST(PostgresCliTest, InsertsQuoteAndHandlesNullableChange) {
    FakeRepository repository;
    std::ostringstream out, err;
    PostgresCli cli{repository, out, err};
    EXPECT_EQ(cli.run({"quote", "aapl", "av", "2026-08-17T12:30:45.123Z",
                       "101.25", "100", "102", "99.5", "1000", "-"}), 0);
    ASSERT_TRUE(repository.quote);
    EXPECT_EQ(repository.quote->symbol, "AAPL");
    EXPECT_EQ(repository.quote->source, "AV");
    EXPECT_EQ(repository.quote->price.to_string(), "101.25000000");
    EXPECT_FALSE(repository.quote->change_pct);
    EXPECT_NE(out.str().find("inserted=1"), std::string::npos);
}

TEST(PostgresCliTest, ReportsUsageWithoutCallingRepository) {
    FakeRepository repository;
    std::ostringstream out, err;
    PostgresCli cli{repository, out, err};
    EXPECT_EQ(cli.run({"tick", "AAPL"}), 2);
    EXPECT_NE(err.str().find("usage: tick"), std::string::npos);
    EXPECT_FALSE(repository.tick);
}

TEST(PostgresCliTest, LatestQuoteMissUsesDistinctExitCode) {
    FakeRepository repository;
    std::ostringstream out, err;
    PostgresCli cli{repository, out, err};
    EXPECT_EQ(cli.run({"latest-quote", "aapl", "av"}), 3);
    EXPECT_EQ(repository.latest_symbol, "AAPL");
    EXPECT_EQ(repository.latest_source, "AV");
    EXPECT_EQ(out.str(), "not found\n");
}

} // namespace
} // namespace fincore::cli
