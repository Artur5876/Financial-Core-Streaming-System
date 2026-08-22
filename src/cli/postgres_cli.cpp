#include "cli/postgres_cli.hpp"

#include "domain/decimal.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace fincore::cli {
namespace {

using domain::Decimal;
using domain::Timestamp; //std::chrono::system_clock::time_point

// Get all value(query) in Upper-case
[[nodiscard]] std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

[[nodiscard]] bool parse_bool(std::string value) {
    value = upper(std::move(value));
    if (value == "TRUE" || value == "1" || value == "YES") return true;
    if (value == "FALSE" || value == "0" || value == "NO") return false;
    throw std::invalid_argument("boolean must be true or false");
}

[[nodiscard]] std::int16_t parse_tick_decimals(const std::string& value) {
    std::size_t consumed{};
    const long parsed = std::stol(value, &consumed);
    if (consumed != value.size() || parsed < 0 || parsed > 8) {
        throw std::invalid_argument("TICK_DECIMALS must be an integer from 0 to 8");
    }
    return static_cast<std::int16_t>(parsed);
}

[[nodiscard]] Timestamp parse_timestamp(const std::string& value) {
    if (upper(value) == "NOW") return std::chrono::system_clock::now();

    // Accepted form: YYYY-MM-DDTHH:MM:SS[.ffffff]Z
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
        value.back() != 'Z') {
        throw std::invalid_argument(
            "timestamp must be UTC RFC3339 (YYYY-MM-DDTHH:MM:SS[.ffffff]Z) or 'now'");
    }

    std::tm utc{};
    std::istringstream input(value.substr(0, 19));
    input >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) throw std::invalid_argument("timestamp contains an invalid date or time");

    std::int64_t micros{};
    if (value.size() > 20) {
        if (value[19] != '.') throw std::invalid_argument("invalid timestamp fractional seconds");
        const std::string fraction = value.substr(20, value.size() - 21);
        if (fraction.empty() || fraction.size() > 6 ||
            !std::all_of(fraction.begin(), fraction.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            throw std::invalid_argument("timestamp supports one to six fractional digits");
        }
        micros = std::stoll(fraction);
        for (std::size_t i = fraction.size(); i < 6; ++i) micros *= 10;
    }

    const std::time_t seconds = timegm(&utc);
    if (seconds == static_cast<std::time_t>(-1)) {
        throw std::invalid_argument("timestamp is outside the supported range");
    }
    const auto timestamp = std::chrono::system_clock::from_time_t(seconds) +
                           std::chrono::microseconds{micros};
    const std::time_t round_trip = std::chrono::system_clock::to_time_t(timestamp);
    std::tm checked{};
    gmtime_r(&round_trip, &checked);
    if (checked.tm_year != utc.tm_year || checked.tm_mon != utc.tm_mon ||
        checked.tm_mday != utc.tm_mday || checked.tm_hour != utc.tm_hour ||
        checked.tm_min != utc.tm_min || checked.tm_sec != utc.tm_sec) {
        throw std::invalid_argument("timestamp contains an invalid calendar date");
    }
    return timestamp;
}

[[nodiscard]] std::string format_timestamp(Timestamp timestamp) {
    using namespace std::chrono;
    const auto micros = floor<microseconds>(timestamp);
    const auto seconds = floor<std::chrono::seconds>(micros);
    const auto fraction = duration_cast<microseconds>(micros - seconds).count();
    const std::time_t raw = system_clock::to_time_t(seconds);
    std::tm utc{};
    gmtime_r(&raw, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setw(6) << std::setfill('0') << fraction << 'Z';
    return out.str();
}

[[nodiscard]] domain::AssetClass parse_asset_class(std::string value) {
    value = upper(std::move(value));
    if (value == "EQUITY") return domain::AssetClass::equity;
    if (value == "FOREX") return domain::AssetClass::forex;
    if (value == "CRYPTO") return domain::AssetClass::crypto;
    if (value == "FUTURES") return domain::AssetClass::futures;
    if (value == "ETF") return domain::AssetClass::etf;
    throw std::invalid_argument("asset class must be EQUITY, FOREX, CRYPTO, FUTURES, or ETF");
}

[[nodiscard]] domain::TradeSide parse_side(std::string value) {
    value = upper(std::move(value));
    if (value == "B") return domain::TradeSide::bid;
    if (value == "A") return domain::TradeSide::ask;
    if (value == "U") return domain::TradeSide::unknown;
    throw std::invalid_argument("side must be B, A, or U");
}

// Command(as an input) sizew validation`
void require_count(const std::vector<std::string>& args, std::size_t count,
                   std::string_view usage) {
    if (args.size() != count) throw std::invalid_argument("usage: " + std::string(usage));
}

void print_summary(std::ostream& out, const persistence::InsertSummary& summary) {
    out << "attempted=" << summary.attempted << " inserted=" << summary.inserted
        << " duplicates=" << summary.duplicates() << '\n';
}

} // namespace

PostgresCli::PostgresCli(persistence::MarketDataRepository& repository,
                         std::ostream& out, std::ostream& err)
    : repository_(repository), out_(out), err_(err) {}

int PostgresCli::run(const std::vector<std::string>& args) {
    try {
        if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
            print_help(out_);
            return 0;
        }

        const std::string command = args[0];
        if (command == "instrument") {
            require_count(args, 7,
                "instrument SYMBOL NAME ASSET_CLASS EXCHANGE TICK_DECIMALS ACTIVE");
            repository_.upsert_instrument({upper(args[1]), args[2], parse_asset_class(args[3]),
                                           args[4], parse_tick_decimals(args[5]), parse_bool(args[6])});
            out_ << "instrument upserted: " << upper(args[1]) << '\n';
        } else if (command == "source") {
            require_count(args, 5, "source CODE DISPLAY_NAME BASE_URL|- ACTIVE");
            const auto url = args[3] == "-" ? std::optional<std::string>{} : args[3];
            repository_.upsert_data_source({upper(args[1]), args[2], url, parse_bool(args[4])});
            out_ << "data source upserted: " << upper(args[1]) << '\n';
        } else if (command == "quote") {
            require_count(args, 10,
                "quote SYMBOL SOURCE TIMESTAMP PRICE OPEN HIGH LOW VOLUME CHANGE_PCT|-");
            domain::Quote value{upper(args[1]), upper(args[2]), parse_timestamp(args[3]),
                Decimal::from_string(args[4]), Decimal::from_string(args[5]),
                Decimal::from_string(args[6]), Decimal::from_string(args[7]),
                Decimal::from_string(args[8]), std::nullopt};
            if (args[9] != "-") value.change_pct = Decimal::from_string(args[9]);
            print_summary(out_, repository_.insert_quotes({&value, 1}));
        } else if (command == "tick") {
            require_count(args, 8, "tick SYMBOL SOURCE TIMESTAMP TRADE_ID|- PRICE SIZE SIDE");
            domain::Tick value{upper(args[1]), upper(args[2]), parse_timestamp(args[3]),
                args[4] == "-" ? std::optional<std::string>{} : args[4],
                Decimal::from_string(args[5]), Decimal::from_string(args[6]), parse_side(args[7])};
            print_summary(out_, repository_.insert_ticks({&value, 1}));
        } else if (command == "snapshot") {
            require_count(args, 9,
                "snapshot SYMBOL SOURCE TIMESTAMP BEST_BID BEST_ASK IMBALANCE BID_VOLUME ASK_VOLUME");
            domain::OrderBookSnapshot value{upper(args[1]), upper(args[2]), parse_timestamp(args[3]),
                Decimal::from_string(args[4]), Decimal::from_string(args[5]),
                Decimal::from_string(args[6]), Decimal::from_string(args[7]),
                Decimal::from_string(args[8])};
            print_summary(out_, repository_.insert_order_book_snapshots({&value, 1}));
        } else if (command == "indicator") {
            require_count(args, 7,
                "indicator SYMBOL SOURCE NAME TIMESTAMP VALUE PARAMETERS_JSON");
            domain::TechnicalIndicator value{upper(args[1]), upper(args[2]), upper(args[3]),
                parse_timestamp(args[4]), Decimal::from_string(args[5]), args[6]};
            print_summary(out_, repository_.insert_technical_indicators({&value, 1}));
        } else if (command == "latest-quote") {
            require_count(args, 3, "latest-quote SYMBOL SOURCE");
            const auto value = repository_.latest_quote(upper(args[1]), upper(args[2]));
            if (!value) {
                out_ << "not found\n";
                return 3;
            }
            out_ << "symbol=" << value->symbol << " source=" << value->source
                 << " timestamp=" << format_timestamp(value->timestamp)
                 << " price=" << value->price.to_string()
                 << " open=" << value->open.to_string()
                 << " high=" << value->high.to_string()
                 << " low=" << value->low.to_string()
                 << " volume=" << value->volume.to_string()
                 << " change_pct=" << (value->change_pct ? value->change_pct->to_string() : "null")
                 << '\n';
        } else {
            throw std::invalid_argument("unknown command: " + command + " (run with --help)");
        }
        return 0;
    } catch (const std::invalid_argument& error) {
        err_ << "error: " << error.what() << '\n';
        return 2;
    } catch (const std::out_of_range& error) {
        err_ << "error: " << error.what() << '\n';
        return 2;
    }
}

void PostgresCli::print_help(std::ostream& out) {
    out << "FinCore PostgreSQL CLI\n\n"
        << "Usage: fincore_app postgres COMMAND [ARGUMENTS]\n\n"
        << "Commands:\n"
        << "  instrument SYMBOL NAME ASSET_CLASS EXCHANGE TICK_DECIMALS ACTIVE\n"
        << "  source CODE DISPLAY_NAME BASE_URL|- ACTIVE\n"
        << "  quote SYMBOL SOURCE TIMESTAMP PRICE OPEN HIGH LOW VOLUME CHANGE_PCT|-\n"
        << "  tick SYMBOL SOURCE TIMESTAMP TRADE_ID|- PRICE SIZE SIDE\n"
        << "  snapshot SYMBOL SOURCE TIMESTAMP BEST_BID BEST_ASK IMBALANCE BID_VOLUME ASK_VOLUME\n"
        << "  indicator SYMBOL SOURCE NAME TIMESTAMP VALUE PARAMETERS_JSON\n"
        << "  latest-quote SYMBOL SOURCE\n\n"
        << "TIMESTAMP is 'now' or UTC RFC3339; quote dependencies must be upserted first.\n";
}

} // namespace fincore::cli
