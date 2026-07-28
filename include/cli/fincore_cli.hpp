#pragma once

#include "api/alpha_vantage_client.hpp"
#include "cli/process_metrics.hpp"
#include "core/order_book.hpp"
#include "core/types.hpp"
#include "storage/redis_client.hpp"

#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fincore::cli {

struct CliServices {
    std::function<std::optional<Quote>(const Symbol&)> get_quote;
    std::function<bool()> last_was_cached;

    std::function<bool()> redis_is_connected;
    std::function<std::optional<Quote>(const Symbol&)> get_cached_quote;

    std::function<bool(
        const Symbol&,
        const Quote&)> store_quote;

    std::function<bool(
        const Symbol&,
        const std::map<Price, Volume>&,
        const std::map<Price, Volume>&)> update_order_book;
};

class FinCoreCli {
public:
    // Production constructor.
    FinCoreCli(
        AlphaVantageClient& av_client,
        RedisClient& redis,
        std::vector<std::string> symbols,
        int default_poll_seconds,
        std::istream& in,
        std::ostream& out);

    // Unit-test constructor.
    FinCoreCli(
        CliServices services,
        std::vector<std::string> symbols,
        int default_poll_seconds,
        std::istream& in,
        std::ostream& out);

    int run();

private:
    using Args = std::vector<std::string>;

    void handle_line(const std::string& line);
    void print_help() const;
    void print_quote(const Quote& quote) const;
    void print_snapshot(const OrderBookSnapshot& snapshot) const;

    void handle_symbols(const Args& args) const;
    void handle_fetch(const Args& args);
    void handle_book(const Args& args) const;
    void handle_lookup(const Args& args) const;
    void handle_watch(const Args& args);
    void handle_poll(const Args& args);
    void handle_redis(const Args& args) const;
    void handle_stats(const Args& args);

    bool fetch_symbol(
        const std::string& raw_symbol,
        bool print_data,
        bool print_metrics);

    void fetch_all(bool print_data, bool print_metrics);

    [[nodiscard]] bool is_configured(
        const std::string& symbol) const;

    //method that will take string and return std::vector<std::string>
    [[nodiscard]] static Args tokenize(
        const std::string& line);

    [[nodiscard]] static std::string normalize_symbol(
        std::string symbol);

    [[nodiscard]] static std::size_t parse_number(
        const std::string& text,
        const char* name,
        std::size_t minimum,
        std::size_t maximum);

    CliServices services_;

    std::vector<std::string> symbols_;
    std::unordered_map<std::string, OrderBook> books_;
    std::unordered_set<std::string> books_with_data_;

    int default_poll_seconds_;
    std::istream& in_;
    std::ostream& out_;
    bool running_{true};
    SessionStats stats_;
};

} // namespace fincore::cli
