#include "cli/fincore_cli.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace fincore::cli {
namespace {

using Clock = std::chrono::steady_clock;

std::int64_t elapsed_us(Clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started).count();
}

template <typename Redis>
bool update_order_book_checked(
    Redis& redis,
    const std::string& symbol,
    const std::map<Price, Volume>& bids,
    const std::map<Price, Volume>& asks) {
    using Result = decltype(redis.update_order_book(symbol, bids, asks));

    if constexpr (std::is_void_v<Result>) {
        redis.update_order_book(symbol, bids, asks);
        return true;
    } else {
        return static_cast<bool>(
            redis.update_order_book(symbol, bids, asks));
    }
}

void populate_book_from_quote(OrderBook& book, const Quote& quote) {
    book.clear();

    constexpr double tick = 0.01;

    for (int level = 1; level <= 5; ++level) {
        const Price price = quote.price - (static_cast<double>(level) * tick);
        const Volume volume = static_cast<Volume>(quote.volume / 10 / level);

        if (price > 0.0 && volume > 0) {
            book.set_bid(price, volume);
        }
    }

    for (int level = 1; level <= 5; ++level) {
        const Price price = quote.price + (static_cast<double>(level) * tick);
        const Volume volume = static_cast<Volume>(quote.volume / 10 / level);

        if (price > 0.0 && volume > 0) {
            book.set_ask(price, volume);
        }
    }
}

} // namespace


//Construction of function wrappers (for unit-tests)
CliServices make_cli_services(AlphaVantageClient& av_client, RedisClient& redis) {
    CliServices services;

    services.get_quote = [&av_client](const Symbol& symbol) {
        return av_client.get_quote(symbol);
    };

    services.last_was_cached = [&av_client] {
        return av_client.last_was_cached();
    };

    services.redis_is_connected = [&redis]{
        return redis.is_connected();
    };

    services.get_cached_quote = [&redis](const Symbol& symbol) {
        return redis.get_quote(symbol);
    };

    services.store_quote = [&redis] (const Symbol& symbol,
                                    const Quote& quote)
    {
        return redis.store_quote(symbol, quote);
    };

    services.update_order_book = [&redis](const Symbol& symbol,
                                        const std::map<Price, Volume>& bids,
                                        const std::map<Price, Volume>& asks)
    {
        return update_order_book_checked(redis, symbol, bids, asks);
    };

    return services;
}

FinCoreCli::FinCoreCli(AlphaVantageClient& av_client,
                       RedisClient& redis,
                       std::vector<std::string> symbols,
                       int default_poll_seconds,
                       std::istream& in,
                       std::ostream& out)
    : FinCoreCli(
        make_cli_services(av_client, redis),
        std::move(symbols),
        default_poll_seconds,
        in,
        out){}


FinCoreCli::FinCoreCli(CliServices services,
                       std::vector<std::string> symbols,
                       int default_poll_seconds,
                       std::istream& in,
                       std::ostream& out)
    : services_(std::move(services)),
      symbols_(std::move(symbols)),
      default_poll_seconds_(default_poll_seconds),
      in_(in),
      out_(out) {
    for (auto& symbol : symbols_) {
        symbol = normalize_symbol(std::move(symbol));
        books_.try_emplace(symbol, symbol);
    }
}

int FinCoreCli::run() {
    out_ << "\nFinCore Interactive CLI\n"
         << "Type 'help' to list commands.\n\n";

    std::string line;
    while (running_) {
        out_ << "fincore> " << std::flush;

        if (!std::getline(in_, line)) {
            out_ << '\n';
            break;
        }

        try {
            handle_line(line);
        } catch (const std::exception& error) {
            out_ << "[ERROR] " << error.what() << '\n';
        }
    }

    return 0;
}

void FinCoreCli::handle_line(const std::string& line) {
    auto args = tokenize(line); //std::vector<std::string>
    if (args.empty()) {
        return;
    }

    //converting to upper case (user input)
    std::string command = normalize_symbol(std::move(args.front()));

    // Converting all characters to lower case in case if there are any uppercase characters
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    // Deleting the first element from the vector(first word)
    args.erase(args.begin());

    if (command == "help") {
        print_help();
    } else if (command == "symbols") {
        handle_symbols(args);
    } else if (command == "fetch") {
        handle_fetch(args);
    } else if (command == "book") {
        handle_book(args);
    } else if (command == "lookup") { //benchmark test
        handle_lookup(args);
    } else if (command == "watch") {
        handle_watch(args);
    } else if (command == "poll") {
        handle_poll(args);
    } else if (command == "redis") {
        handle_redis(args);
    } else if (command == "stats") {
        handle_stats(args);
    } else if (command == "exit" || command == "quit") {
        running_ = false;
        out_ << "Bye.\n";
    } else {
        out_ << "[ERROR] unknown command: " << command
             << ". Type 'help'.\n";
    }
}

void FinCoreCli::print_help() const {
    out_ << "Commands:\n"
         << "  help                              Show this help\n"
         << "  symbols                           Show configured symbols\n"
         << "  fetch <SYMBOL|all>                API -> Redis -> OrderBook -> Redis\n"
         << "  book <SYMBOL>                     Show current in-memory book snapshot\n"
         << "  lookup <SYMBOL> [COUNT]           Benchmark vector/map lookup in ns\n"
         << "  watch <SYMBOL|all> [COUNT] [MS]   Repeat a fetch operation\n"
         << "  poll [COUNT] [SECONDS]            Repeat full-symbol polling cycles\n"
         << "  redis status                      Show Redis connection state\n"
         << "  stats                             Show aggregate latency/resource stats\n"
         << "  stats last                        Show the last measured operation\n"
         << "  stats reset                       Reset session measurements\n"
         << "  exit | quit                       Exit\n";
}

void FinCoreCli::print_quote(const Quote& quote) const {
    out_ << "\n--- Quote: " << quote.symbol << " ---------------------\n"
         << std::fixed << std::setprecision(4)
         << "  Price  : $" << quote.price << '\n'
         << "  Open   : $" << quote.open << '\n'
         << "  High   : $" << quote.high << '\n'
         << "  Low    : $" << quote.low << '\n'
         << "  Volume :  " << quote.volume << '\n'
         << "  Chg %  :  " << quote.change_pct << "%\n"
         << "  Source :  " << quote.source << '\n';
}

void FinCoreCli::print_snapshot(const OrderBookSnapshot& snapshot) const {
    out_ << "\n--- OrderBook Snapshot: " << snapshot.symbol
         << " -----------------\n"
         << std::fixed << std::setprecision(4)
         << "  Best Bid : $" << snapshot.best_bid << '\n'
         << "  Best Ask : $" << snapshot.best_ask << '\n'
         << "  Mid      : $" << snapshot.mid_price << '\n'
         << "  Spread   : $" << snapshot.spread << '\n'
         << "  Imbalance:  " << snapshot.imbalance << '\n'
         << "  Bid Vol  :  " << snapshot.total_bid_vol << '\n'
         << "  Ask Vol  :  " << snapshot.total_ask_vol << '\n';
}

void FinCoreCli::handle_symbols(const Args& args) const {
    if (!args.empty()) {
        throw std::invalid_argument("usage: symbols");
    }

    out_ << "Configured symbols (" << symbols_.size() << "):\n";
    for (const auto& symbol : symbols_) {
        out_ << "  - " << symbol << '\n';
    }
}

void FinCoreCli::handle_fetch(const Args& args) {
    if (args.size() != 1) {
        throw std::invalid_argument("usage: fetch <SYMBOL|all>");
    }

    // Taking second attribute(word) from user command
    std::string target = normalize_symbol(args.front());
    if (target == "ALL") {
        fetch_all(true, true);
        return;
    }

    fetch_symbol(target, true, true);//symbol and metrics data is included
}

void FinCoreCli::handle_book(const Args& args) const {

    if (args.size() != 1) {
        throw std::invalid_argument("usage: book <SYMBOL>");
    }

    const std::string symbol = normalize_symbol(args.front());
    if (!is_configured(symbol)) {
        throw std::invalid_argument("symbol is not configured: " + symbol);
    }

    if (books_with_data_.find(symbol) == books_with_data_.end()) {
        out_ << "[MISS] no in-memory order book data for " << symbol
             << ". Run 'fetch " << symbol << "' first.\n";
        return;
    }

    const auto started = Clock::now();
    const auto& book = books_.at(symbol);
    const auto snapshot = book.snapshot();
    const auto lookup_us = elapsed_us(started);

    print_snapshot(snapshot);
    out_ << "[lookup] unordered_map + snapshot time: " << lookup_us << " us\n";
}

void FinCoreCli::handle_lookup(const Args& args) const {
    if (args.empty() || args.size() > 2) {
        throw std::invalid_argument("usage: lookup <SYMBOL> [COUNT]");
    }

    const std::string symbol = normalize_symbol(args.front());
    const std::size_t count = args.size() == 2
        ? parse_number(args[1], "COUNT", 1, 100'000'000)
        : 1'000'000;

    if (!is_configured(symbol)) {
        throw std::invalid_argument("symbol is not configured: " + symbol);
    }

    const auto process_before = read_process_snapshot();
    const auto benchmark_started = Clock::now();

    volatile std::size_t vector_hits = 0;
    const auto vector_started = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto found = std::find(symbols_.begin(), symbols_.end(), symbol);
        if (found != symbols_.end()) {
            ++vector_hits;
        }
    }
    const auto vector_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - vector_started).count();

    volatile std::size_t map_hits = 0;
    const auto map_started = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto found = books_.find(symbol);
        if (found != books_.end()) {
            ++map_hits;
        }
    }
    const auto map_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - map_started).count();

    const auto benchmark_wall_us = elapsed_us(benchmark_started);
    const auto process_after = read_process_snapshot();
    const auto usage = process_delta(process_before, process_after);

    out_ << "\n=== Lookup Benchmark =========================================\n"
         << "  symbol               " << symbol << '\n'
         << "  iterations           " << count << '\n'
         << std::fixed << std::setprecision(2)
         << "  vector std::find     "
         << static_cast<double>(vector_ns) / static_cast<double>(count)
         << " ns/op (hits=" << vector_hits << ")\n"
         << "  unordered_map::find  "
         << static_cast<double>(map_ns) / static_cast<double>(count)
         << " ns/op (hits=" << map_hits << ")\n"
         << "  user CPU             "
         << static_cast<double>(usage.user_cpu_us) / 1'000.0 << " ms\n"
         << "  system CPU           "
         << static_cast<double>(usage.system_cpu_us) / 1'000.0 << " ms\n"
         << "  process CPU / wall   "
         << usage.cpu_percent(benchmark_wall_us) << "%\n"
         << "  current RSS          "
         << static_cast<double>(usage.current_rss_kb) / 1024.0 << " MiB\n"
         << "  RSS delta            "
         << static_cast<double>(usage.rss_delta_kb) / 1024.0 << " MiB\n"
         << "==============================================================\n";
}

void FinCoreCli::handle_watch(const Args& args) {
    if (args.empty() || args.size() > 3) {
        throw std::invalid_argument(
            "usage: watch <SYMBOL|all> [COUNT] [INTERVAL_MS]");
    }

    const std::string target = normalize_symbol(args[0]);
    const std::size_t count = args.size() >= 2
        ? parse_number(args[1], "COUNT", 1, 100'000)
        : 10;
    const std::size_t interval_ms = args.size() == 3
        ? parse_number(args[2], "INTERVAL_MS", 0, 3'600'000)
        : 1'000;

    if (target != "ALL" && !is_configured(target)) {
        throw std::invalid_argument("symbol is not configured: " + target);
    }

    out_ << "Watching " << target
         << " count=" << count
         << " interval_ms=" << interval_ms << '\n';

    for (std::size_t index = 0; index < count; ++index) {
        out_ << "\n[watch " << (index + 1) << '/' << count << "]\n";

        if (target == "ALL") {
            fetch_all(false, false);
        } else {
            fetch_symbol(target, false, true);
        }

        if (index + 1 < count && interval_ms > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(interval_ms));
        }
    }

    stats_.print_summary(out_);
}

void FinCoreCli::handle_poll(const Args& args) {
    if (args.size() > 2) {
        throw std::invalid_argument("usage: poll [COUNT] [SECONDS]");
    }

    const std::size_t count = args.empty()
        ? 1
        : parse_number(args[0], "COUNT", 1, 100'000);

    const std::size_t seconds = args.size() == 2
        ? parse_number(args[1], "SECONDS", 0, 86'400)
        : static_cast<std::size_t>(std::max(default_poll_seconds_, 0));

    for (std::size_t cycle = 0; cycle < count; ++cycle) {
        out_ << "\n[cycle " << (cycle + 1) << '/' << count << "]\n";
        fetch_all(true, false);

        if (cycle + 1 < count && seconds > 0) {
            out_ << "[FinCore] Sleeping for " << seconds << "s ...\n";
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
        }
    }

    stats_.print_summary(out_);
}

void FinCoreCli::handle_redis(const Args& args) const {
    if (args.size() != 1 || normalize_symbol(args.front()) != "STATUS") {
        throw std::invalid_argument("usage: redis status");
    }

    out_ << (services_.redis_is_connected()
        ? "[OK] Redis is connected\n"
        : "[ERROR] Redis is disconnected\n");
}

void FinCoreCli::handle_stats(const Args& args) {
    if (args.empty()) {
        stats_.print_summary(out_);
        return;
    }

    if (args.size() != 1) {
        throw std::invalid_argument("usage: stats [last|reset]");
    }

    const std::string action = normalize_symbol(args.front());

    if (action == "LAST") {
        stats_.print_last(out_);
    } else if (action == "RESET") {
        stats_.reset();
        out_ << "[OK] session statistics reset\n";
    } else {
        throw std::invalid_argument("usage: stats [last|reset]");
    }
}

bool FinCoreCli::fetch_symbol(const std::string& raw_symbol,
                              bool print_data,
                              bool print_metrics) {
    //second argument from user command
    const std::string symbol = normalize_symbol(raw_symbol);

    //Check if we have quote configured in out orderbook
    if (!is_configured(symbol)) {
        throw std::invalid_argument("symbol is not configured: " + symbol);
    }

    OperationMetrics metrics;
    metrics.symbol = symbol;

    const auto process_before = read_process_snapshot();
    const auto total_started = Clock::now();

    try {
        const auto redis_read_started = Clock::now();
        auto maybe_quote = services_.get_cached_quote(symbol);// Redis retrieval
        metrics.redis_read_us = elapsed_us(redis_read_started);
        //if redis cache hit
        metrics.redis_cache_hit = maybe_quote.has_value();

        //if not in caches than we will implement API request
        if (!maybe_quote) {
            const auto api_started = Clock::now();
            maybe_quote = services_.get_quote(symbol);
            auto started = api_started;
            metrics.api_us = elapsed_us(started);
            metrics.api_cached = services_.last_was_cached();
        }

        // If argument wasnt found in Redis, then we will see the statistic of look up
        if (!maybe_quote) {
            metrics.total_us = elapsed_us(total_started);
            metrics.process = process_delta(process_before, read_process_snapshot());
            stats_.record(metrics);

            out_ << "[MISS] Alpha Vantage returned no quote for " << symbol << '\n';
            if (print_metrics) {
                print_operation_metrics(out_, metrics);
            }
            return false;
        }
        const Quote& quote = *maybe_quote;

        if (metrics.redis_cache_hit) {
            // Do not refresh the TTL or duplicate history on every cache read.
            metrics.redis_quote_stored = true;
        } else {
            const auto redis_quote_started = Clock::now();
            metrics.redis_quote_stored = services_.store_quote(symbol, quote);
            metrics.redis_quote_us = elapsed_us(redis_quote_started);
        }

        auto& book = books_.at(symbol);

        //the time_point of book update
        const auto book_started = Clock::now();
        populate_book_from_quote(book, quote);
        metrics.book_build_us = elapsed_us(book_started);
        books_with_data_.insert(symbol);

        const std::map<Price, Volume> bids_map(
            book.bids().begin(), book.bids().end());
        const std::map<Price, Volume> asks_map(
            book.asks().begin(), book.asks().end());

        const auto redis_book_started = Clock::now();
        metrics.redis_book_stored = services_.update_order_book(
                                                symbol,
                                                bids_map,
                                                asks_map);

        metrics.redis_book_us = elapsed_us(redis_book_started);

        metrics.success = metrics.redis_quote_stored
                       && metrics.redis_book_stored;
        metrics.total_us = elapsed_us(total_started);
        metrics.process = process_delta(process_before, read_process_snapshot());
        stats_.record(metrics);

        out_ << "[OK] " << symbol
             << (metrics.redis_cache_hit
                    ? " [Redis cache]"
                    : (metrics.api_cached
                        ? " [API client cache]"
                        : " [external API]"))
             << " quote_redis="
             << (metrics.redis_cache_hit
                    ? "cache-hit"
                    : (metrics.redis_quote_stored ? "ok" : "failed"))
             << " book_redis=" << (metrics.redis_book_stored ? "ok" : "failed")
             << '\n';

        if (print_data) {
            print_quote(quote);
            print_snapshot(book.snapshot());
        }

        if (print_metrics) {
            print_operation_metrics(out_, metrics);
        }

        return metrics.success;
    } catch (...) {
        metrics.total_us = elapsed_us(total_started);
        metrics.process = process_delta(process_before, read_process_snapshot());
        stats_.record(metrics);
        throw;
    }
}

void FinCoreCli::fetch_all(bool print_data, bool print_metrics) {
    //redis cache check
    if (!services_.redis_is_connected()) {
        out_ << "[WARN] Redis is disconnected; writes may fail.\n";
    }

    std::size_t successes = 0;
    for (const auto& symbol : symbols_) {
        if (fetch_symbol(symbol, print_data, print_metrics)) {
            ++successes;
        }
    }

    out_ << "\n[batch] successful=" << successes
         << " failed=" << (symbols_.size() - successes)
         << " total=" << symbols_.size() << '\n';
}

// If symbol contains in orderbook
bool FinCoreCli::is_configured(const std::string& symbol) const {
    return books_.find(symbol) != books_.end();
}

// Converting the input line into vector of string type in order to read users sentence word by word
FinCoreCli::Args FinCoreCli::tokenize(const std::string& line) {
    std::istringstream input{line};
    Args args;
    std::string token;

    while (input >> token) {
        args.push_back(std::move(token));
    }

    return args;
}

std::string FinCoreCli::normalize_symbol(std::string symbol) {
    std::transform(symbol.begin(), symbol.end(), symbol.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::toupper(ch));
                   });
    return symbol;
}

std::size_t FinCoreCli::parse_number(const std::string& text,
                                     const char* name,
                                     std::size_t minimum,
                                     std::size_t maximum) {
    std::size_t consumed = 0;
    unsigned long long value = 0;

    try {
        value = std::stoull(text, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string{name} + " must be a number");
    }

    if (consumed != text.size() || value < minimum || value > maximum) {
        throw std::invalid_argument(
            std::string{name} + " must be between "
            + std::to_string(minimum) + " and "
            + std::to_string(maximum));
    }

    return static_cast<std::size_t>(value);
}

} // namespace fincore::cli
