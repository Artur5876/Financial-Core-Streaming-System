#include "cli/fincore_cli.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fincore
{
    namespace
    {
        Quote make_quote(const std::string& symbol,
                         double price = 100.0,
                         Volume volume = 1'000)
        {
            Quote quote{};
            quote.symbol = symbol;
            quote.price = price;
            quote.open = price - 1.0;
            quote.high = price + 2.0;
            quote.low = price - 2.0;
            quote.volume = volume;
            quote.change_pct = 1.25;
            quote.source = "TEST";
            return quote;
        }

        struct FakeBackend {
            bool connected{true};
            bool cached{false};
            bool quote_write_result{true};
            bool book_write_result{true};

            //history check lists that will carry data in order to
            //produce statistics(mean, median, deviation etc)
            int quote_calls{};
            int quote_write_calls{};
            int book_write_calls{};
            int connection_checks{};
            int cache_read_calls{};
            std::optional<Quote> cached_quote;

            std::vector<std::string> requested_symbols;
            std::vector<std::string> quote_write_symbols;
            std::vector<std::string> book_write_symbols;

            std::map<Price, Volume> last_bids;
            std::map<Price, Volume> last_asks;

            std::function<std::optional<Quote>(const Symbol&)> quote_handler;
        };

        cli::CliServices make_services(FakeBackend& backend)
        {
            cli::CliServices services;

            services.get_quote =
                [&backend](const Symbol& symbol) {
                    ++backend.quote_calls;
                    backend.requested_symbols.push_back(symbol);

                    //if nothing is foudn than return empty Quote{}
                    if (!backend.quote_handler) {
                        return std::optional<Quote>{};
                    }

                    return backend.quote_handler(symbol);
            };

            services.last_was_cached = [&backend] {
                return backend.cached;
            };

            services.redis_is_connected =
                [&backend] {
                    ++backend.connection_checks;
                    return backend.connected;
            };

            services.get_cached_quote =
                [&backend](const Symbol&) {
                    ++backend.cache_read_calls;
                    return backend.cached_quote;
            };

            services.store_quote =
                [&backend](const Symbol& symbol, const Quote&) {
                    ++backend.quote_write_calls;
                    backend.quote_write_symbols.push_back(symbol);
                    return backend.quote_write_result;
            };

            services.update_order_book =
                [&backend] (const Symbol& symbol,
                        const std::map<Price, Volume>& bids,
                        const std::map<Price, Volume>& asks) {
                    ++backend.book_write_calls;
                    backend.last_bids = bids;
                    backend.last_asks = asks;

                    return backend.book_write_result;
            };
            return services;
        }

        std::string run_cli(FakeBackend& backend,
                const std::string& commands,
                std::vector<std::string> symbols = {"ibm", "aapl"},
                int default_poll_seconds = 0)
        {
            std::istringstream input{commands};
            std::ostringstream output;

            cli::FinCoreCli cli{
                make_services(backend),
                std::move(symbols),
                default_poll_seconds,
                input,
                output
            };

            EXPECT_EQ(cli.run(), 0);

            return output.str();
        }



        TEST(FinCoreCliTest, PrintsStartupMessageAndExits)
        {
            FakeBackend backend;
            const std::string output = run_cli(
                backend,
                "Exit\n");

            EXPECT_NE(
                output.find("FinCore Interactive CLI"),
                std::string::npos);

            EXPECT_NE(
                output.find("Type 'help' to list commands."),
                std::string::npos);

            EXPECT_NE(output.find("fincore>"), std::string::npos);
            EXPECT_NE(output.find("Bye."), std::string::npos);
        }


        TEST(FinCoreCliTest, AcceptsMixedCaseCommands)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "Help\nQuit\n");

            EXPECT_NE(output.find("Commands:"), std::string::npos);
            EXPECT_NE(output.find("fetch <SYMBOL|all>"), std::string::npos);
            EXPECT_NE(output.find("stats reset"), std::string::npos);
            EXPECT_NE(output.find("Bye."), std::string::npos);
        }


        TEST(FinCoreCliTest, PrintsNormalizedConfiguredSymbols)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "SomethingUnknown\nexit\n");

            EXPECT_NE(output.find("[ERROR] unknown command: somethingunknown"),
                std::string::npos);

        }



        TEST(FinCoreCliTest, IgnoresEmptyInputLines)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "\n   \nexit\n");

            EXPECT_EQ(
                output.find("[ERROR]"),
                std::string::npos);

            EXPECT_NE(output.find("Bye."), std::string::npos);

        }

        TEST(FinCoreCliTest, ReportsBookMissBeforeFetch)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "book ibm\nexit\n");

            EXPECT_NE(
                output.find(
                    "[MISS] no in-memory order book data for IBM"),
                std::string::npos);

            EXPECT_NE(
                output.find("Run 'fetch IBM' first."),
                std::string::npos);

        }

        TEST(FincoreCliTest, RejectsBookForUnconfiguredSymbol)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "book msft\nexit\n");

            EXPECT_NE(
                output.find(
                    "[ERROR] symbol is not configured: MSFT"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, PrintsConnectedRedisStatus)
        {
            FakeBackend backend;
            backend.connected = true;

            const std::string output = run_cli(
                backend,
                "redis status\nexit\n");

            EXPECT_NE(
                output.find("[OK] Redis is connected"),
                std::string::npos);

            EXPECT_EQ(backend.connection_checks, 1);
        }

        TEST(FincoreCliTest, PrintsDisconnectedRedisStatus)
        {
            FakeBackend backend;
            backend.connected = false;

            const std::string output = run_cli(
                backend,
                "redis status\nexit\n");

            EXPECT_NE(
                output.find("[ERROR] Redis is disconnected"),
                std::string::npos);
        }

        TEST(FincoreCliTest, RejectsInvalidRedisCommand)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "redis ping\nexit\n");

            EXPECT_NE(
                output.find("[ERROR] usage: redis status"),
                std::string::npos);
        }


        TEST(FinCoreCliTest, FetchesQuoteBuildsBookAndWritesRedis)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol, 100.0, 1'000)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            ASSERT_EQ(backend.quote_calls, 1);
            ASSERT_EQ(backend.requested_symbols.size(), 1);
            EXPECT_EQ(backend.requested_symbols.front(), "IBM");

            EXPECT_EQ(backend.quote_write_calls, 1);
            EXPECT_EQ(backend.book_write_calls, 1);

            ASSERT_EQ(backend.last_bids.size(), 5);
            ASSERT_EQ(backend.last_asks.size(), 5);

            EXPECT_NEAR(
                backend.last_bids.rbegin()->first,
                99.99,
                1e-9);

            EXPECT_NEAR(
                backend.last_asks.begin()->first,
                100.01,
                1e-9);

            EXPECT_EQ(
                backend.last_bids.rbegin()->second,
                Volume{100});

            EXPECT_EQ(
                backend.last_asks.begin()->second,
                Volume{100});

            EXPECT_NE(
                output.find(
                    "[OK] IBM [external API] quote_redis=ok book_redis=ok"),
                std::string::npos);

            EXPECT_NE(
                output.find("--- Quote: IBM"),
                std::string::npos);

            EXPECT_NE(
                output.find("--- OrderBook Snapshot: IBM"),
                std::string::npos);

            EXPECT_NE(
                output.find("Best Bid : $99.9900"),
                std::string::npos);

            EXPECT_NE(
                output.find("Best Ask : $100.0100"),
                std::string::npos);

            EXPECT_NE(
                output.find("[metrics] IBM SUCCESS"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, MarksCachedApiResponses)
        {
            FakeBackend backend;
            backend.cached = true;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            EXPECT_NE(
                output.find("[OK] IBM [API client cache]"),
                std::string::npos);

            EXPECT_NE(
                output.find("AlphaVantageClient cache"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, RedisCacheHitSkipsApiAndRefreshesDerivedData)
        {
            FakeBackend backend;
            backend.cached_quote = make_quote("IBM", 101.0, 2'000);

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            EXPECT_EQ(backend.cache_read_calls, 1);
            EXPECT_EQ(backend.quote_calls, 0);
            EXPECT_EQ(backend.quote_write_calls, 0);
            EXPECT_EQ(backend.book_write_calls, 1);
            EXPECT_NE(
                output.find("[OK] IBM [Redis cache] quote_redis=cache-hit"),
                std::string::npos);
            EXPECT_NE(output.find("Redis cache         hit"), std::string::npos);
        }

        TEST(FinCoreCliTest, HandlesMissingApiQuote)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol&) {
                    return std::optional<Quote>{};
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            EXPECT_EQ(backend.quote_calls, 1);
            EXPECT_EQ(backend.quote_write_calls, 0);
            EXPECT_EQ(backend.book_write_calls, 0);

            EXPECT_NE(
                output.find(
                    "[MISS] Alpha Vantage returned no quote for IBM"),
                std::string::npos);

            EXPECT_NE(
                output.find("[metrics] IBM FAILED"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, RedisQuoteFailureMakesOperationFail)
        {
            FakeBackend backend;
            backend.quote_write_result = false;
            backend.book_write_result = true;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            EXPECT_EQ(backend.quote_write_calls, 1);
            EXPECT_EQ(backend.book_write_calls, 1);

            EXPECT_NE(
                output.find("quote_redis=failed"),
                std::string::npos);

            EXPECT_NE(
                output.find("book_redis=ok"),
                std::string::npos);

            EXPECT_NE(
                output.find("[metrics] IBM FAILED"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, RedisBookFailureMakesOperationFail)
        {
            FakeBackend backend;
            backend.quote_write_result = true;
            backend.book_write_result = false;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nexit\n");

            EXPECT_NE(
                output.find("quote_redis=ok"),
                std::string::npos);

            EXPECT_NE(
                output.find("book_redis=failed"),
                std::string::npos);

            EXPECT_NE(
                output.find("[metrics] IBM FAILED"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, FetchAllProcessesEveryConfiguredSymbol)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch all\nexit\n",
                {"ibm", "aapl", "msft"});

            EXPECT_EQ(backend.quote_calls, 3);
            EXPECT_EQ(backend.quote_write_calls, 3);
            EXPECT_EQ(backend.book_write_calls, 3);

            EXPECT_EQ(
                backend.requested_symbols,
                (std::vector<std::string>{"IBM", "AAPL", "MSFT"}));

            EXPECT_NE(
                output.find(
                    "[batch] successful=3 failed=0 total=3"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, FetchAllWarnsWhenRedisIsDisconnected)
        {
            FakeBackend backend;
            backend.connected = false;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch all\nexit\n",
                {"ibm"});

            EXPECT_NE(
                output.find(
                    "[WARN] Redis is disconnected; writes may fail."),
                std::string::npos);
        }

        TEST(FinCoreCliTest, BookCanBeReadAfterSuccessfulFetch)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol, 50.0, 2'000)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\nbook ibm\nexit\n");

            const std::size_t first_snapshot =
                output.find("--- OrderBook Snapshot: IBM");

            ASSERT_NE(first_snapshot, std::string::npos);

            const std::size_t second_snapshot =
                output.find(
                    "--- OrderBook Snapshot: IBM",
                    first_snapshot + 1);

            EXPECT_NE(second_snapshot, std::string::npos);

            EXPECT_NE(
                output.find(
                    "[lookup] unordered_map + snapshot time:"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, ValidatesFetchArguments)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "fetch\nfetch ibm extra\nexit\n");

            const std::string expected =
                "[ERROR] usage: fetch <SYMBOL|all>";

            const std::size_t first = output.find(expected);
            ASSERT_NE(first, std::string::npos);

            EXPECT_NE(
                output.find(expected, first + 1),
                std::string::npos);

            EXPECT_EQ(backend.quote_calls, 0);
        }

        TEST(FinCoreCliTest, RejectsUnconfiguredFetchSymbol)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "fetch msft\nexit\n");

            EXPECT_NE(
                output.find(
                    "[ERROR] symbol is not configured: MSFT"),
                std::string::npos);

            EXPECT_EQ(backend.quote_calls, 0);
        }

        TEST(FinCoreCliTest, StatsLastReportsNoOperationInitially)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "stats last\nexit\n");

            EXPECT_NE(
                output.find("No measured operation yet."),
                std::string::npos);
        }

        TEST(FinCoreCliTest, StatsResetClearsRecordedOperations)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "fetch ibm\n"
                "stats reset\n"
                "stats last\n"
                "exit\n");

            EXPECT_NE(
                output.find("[OK] session statistics reset"),
                std::string::npos);

            EXPECT_NE(
                output.find("No measured operation yet."),
                std::string::npos);
        }

        TEST(FinCoreCliTest, LookupUsesExplicitIterationCount)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "lookup ibm 7\nexit\n");

            EXPECT_NE(
                output.find("iterations           7"),
                std::string::npos);

            EXPECT_NE(
                output.find("hits=7"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, LookupRejectsInvalidCount)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "lookup ibm invalid\nexit\n");

            EXPECT_NE(
                output.find("[ERROR] COUNT must be a number"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, LookupRejectsZeroCount)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "lookup ibm 0\nexit\n");

            EXPECT_NE(
                output.find(
                    "[ERROR] COUNT must be between 1 and 100000000"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, WatchRepeatsRequestedNumberOfTimes)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "watch ibm 2 0\nexit\n");

            EXPECT_EQ(backend.quote_calls, 2);
            EXPECT_EQ(backend.quote_write_calls, 2);
            EXPECT_EQ(backend.book_write_calls, 2);

            EXPECT_NE(output.find("[watch 1/2]"), std::string::npos);
            EXPECT_NE(output.find("[watch 2/2]"), std::string::npos);
            EXPECT_NE(
                output.find("FinCore Session Statistics"),
                std::string::npos);
        }

        TEST(FinCoreCliTest, WatchRejectsInvalidCount)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "watch ibm abc 0\nexit\n");

            EXPECT_NE(
                output.find("[ERROR] COUNT must be a number"),
                std::string::npos);

            EXPECT_EQ(backend.quote_calls, 0);
        }

        TEST(FinCoreCliTest, PollProcessesAllSymbolsForEveryCycle)
        {
            FakeBackend backend;

            backend.quote_handler =
                [](const Symbol& symbol) {
                    return std::optional<Quote>{
                        make_quote(symbol)
                    };
                };

            const std::string output = run_cli(
                backend,
                "poll 2 0\nexit\n",
                {"ibm", "aapl"});

            EXPECT_EQ(backend.quote_calls, 4);
            EXPECT_EQ(backend.quote_write_calls, 4);
            EXPECT_EQ(backend.book_write_calls, 4);

            EXPECT_NE(output.find("[cycle 1/2]"), std::string::npos);
            EXPECT_NE(output.find("[cycle 2/2]"), std::string::npos);
        }

        TEST(FinCoreCliTest, PollRejectsTooManyArguments)
        {
            FakeBackend backend;

            const std::string output = run_cli(
                backend,
                "poll 1 0 extra\nexit\n");

            EXPECT_NE(
                output.find("[ERROR] usage: poll [COUNT] [SECONDS]"),
                std::string::npos);
        }



    }
}
