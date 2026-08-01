#include "storage/redis_client.hpp"

#include <gtest/gtest.h>
#include <sw/redis++/redis++.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fincore {
namespace {

constexpr std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0' ? fallback : value;
}

constexpr int redis_port() {
    try {
        return std::stoi(env_or("REDIS_PORT", "6379"));
    } catch (const std::exception&) {
        return 6379;
    }
}

class RedisClientIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<unsigned long> sequence{0};
        test_prefix_ = "fincore_it_" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       "_" + std::to_string(sequence++);

        const std::string host = env_or("REDIS_HOST", "127.0.0.1");
        const int port = redis_port();

        try {
            verifier_ = std::make_unique<sw::redis::Redis>(
                "tcp://" + host + ":" + std::to_string(port));
            verifier_->ping();
            client_ = std::make_unique<RedisClient>(host, port, quote_ttl_);
        } catch (const sw::redis::Error& error) {
            GTEST_SKIP() << "Redis is unavailable at " << host << ':' << port
                         << ": " << error.what();
        }
    }

    // Removes every Redis key that could have been created for the test
    void TearDown() override {
        if (!verifier_) {
            return;
        }

        try {
            for (const auto& symbol : used_symbols_) {
                verifier_->del("quote:" + symbol);
                verifier_->del("quote_history:" + symbol);
                verifier_->del("ticks:" + symbol);
                verifier_->del("latest_tick:" + symbol);
                verifier_->del("order_book:" + symbol + ":bids");
                verifier_->del("order_book:" + symbol + ":asks");
                verifier_->del("order_book:" + symbol + ":timestamp");
            }
        } catch (const sw::redis::Error&) {
            // Cleanup must not hide the test result if Redis stops mid-test.
        }
    }

    Quote make_test_quote(const Symbol& symbol) const {
        Quote quote;
        quote.symbol = symbol;
        quote.price = 189.75;
        quote.open = 188.00;
        quote.high = 191.20;
        quote.low = 187.50;
        quote.volume = 42'000'000;
        quote.change_pct = 0.93;
        quote.source = "INTEGRATION_TEST";
        quote.timestamp = TimePoint(std::chrono::microseconds(
            1'700'000'000'123'456LL));
        return quote;
    }

    constexpr Symbol symbol(const std::string& suffix) {
        Symbol result = test_prefix_ + "_" + suffix;
        used_symbols_.push_back(result);
        return result;
    }

    //quote time bareer retrieval (5 sec)
    static constexpr std::chrono::seconds quote_ttl_{5};
    std::string test_prefix_;
    std::vector<Symbol> used_symbols_;
    // Calls Redis_Client method
    std::unique_ptr<RedisClient> client_;
    // Directly inspects Redis ti confirm what was actually stored
    std::unique_ptr<sw::redis::Redis> verifier_;
};

TEST_F(RedisClientIntegrationTest, ConnectsToRedis) {
    ASSERT_NE(client_, nullptr);
    EXPECT_TRUE(client_->is_connected());
}

TEST_F(RedisClientIntegrationTest, StoresAndReadsQuoteWithTtlAndHistory) {
    const Symbol test_symbol = symbol("quote");
    const Quote expected = make_test_quote(test_symbol);

    ASSERT_TRUE(client_->store_quote(test_symbol, expected));

    const auto actual = client_->get_quote(test_symbol);
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->symbol, expected.symbol);
    EXPECT_DOUBLE_EQ(actual->price, expected.price);
    EXPECT_DOUBLE_EQ(actual->open, expected.open);
    EXPECT_DOUBLE_EQ(actual->high, expected.high);
    EXPECT_DOUBLE_EQ(actual->low, expected.low);
    EXPECT_EQ(actual->volume, expected.volume);
    EXPECT_DOUBLE_EQ(actual->change_pct, expected.change_pct);
    EXPECT_EQ(actual->source, expected.source);
    EXPECT_EQ(actual->timestamp, expected.timestamp);

    const auto ttl = verifier_->ttl("quote:" + test_symbol);
    EXPECT_GT(ttl, 0);
    EXPECT_LE(ttl, quote_ttl_.count());
    EXPECT_EQ(verifier_->llen("quote_history:" + test_symbol), 1);
}

TEST_F(RedisClientIntegrationTest, MissingQuoteReturnsNullopt) {
    EXPECT_FALSE(client_->get_quote(symbol("missing")).has_value());
}

TEST_F(RedisClientIntegrationTest, QuoteExpires) {
    const Symbol test_symbol = symbol("expiry");
    ASSERT_TRUE(client_->store_quote(test_symbol, make_test_quote(test_symbol)));

    std::this_thread::sleep_for(quote_ttl_ + std::chrono::seconds{1});

    EXPECT_FALSE(client_->get_quote(test_symbol).has_value());
    EXPECT_EQ(verifier_->exists("quote:" + test_symbol), 0);
    // History is retained independently from the current-quote cache.
    EXPECT_EQ(verifier_->llen("quote_history:" + test_symbol), 1);
}

TEST_F(RedisClientIntegrationTest, StoresTickInStreamAndLatestValue) {
    const Symbol test_symbol = symbol("tick");
    Tick tick;
    tick.symbol = test_symbol;
    tick.price = 101.25;
    tick.size = 750;
    tick.side = Side::Ask;
    tick.timestamp = TimePoint(std::chrono::microseconds(1'700'000'000'654'321LL));

    ASSERT_TRUE(client_->store_tick(tick));

    EXPECT_EQ(verifier_->xlen("ticks:" + test_symbol), 1);
    EXPECT_EQ(verifier_->get("latest_tick:" + test_symbol),
              std::optional<std::string>{"101.250000:750"});
}

TEST_F(RedisClientIntegrationTest, ReplacesOrderBookSnapshot) {
    const Symbol test_symbol = symbol("book");
    ASSERT_TRUE(client_->update_order_book(
        test_symbol, {{100.0, 10}, {99.5, 20}}, {{100.5, 30}, {101.0, 40}}));
    ASSERT_TRUE(client_->update_order_book(
        test_symbol, {{100.25, 50}}, {{100.75, 60}}));

    std::unordered_map<std::string, std::string> bids;
    std::unordered_map<std::string, std::string> asks;
    verifier_->hgetall("order_book:" + test_symbol + ":bids",
                       std::inserter(bids, bids.end()));
    verifier_->hgetall("order_book:" + test_symbol + ":asks",
                       std::inserter(asks, asks.end()));

    EXPECT_EQ(bids,
              (std::unordered_map<std::string, std::string>{{"100.250000", "50"}}));
    EXPECT_EQ(asks,
              (std::unordered_map<std::string, std::string>{{"100.750000", "60"}}));
    EXPECT_TRUE(verifier_->exists("order_book:" + test_symbol + ":timestamp"));
}

}  // namespace
}  // namespace fincore
