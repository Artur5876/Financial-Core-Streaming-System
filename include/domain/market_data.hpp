#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace fincore::domain {
    using Timestamp = std::chrono::system_clock::time_point;

    enum class AssetClass {
        equity,
        forex,
        crypto,
        futures,
        etf
    };

    enum class TradeSide : char {
        bid = 'B',
        ask = 'A',
        unknown = 'U'
    };

    struct Instrument {
        std::string symbol;
        std::string name;
        AssetClass asset_class{};
        std::string exchange;
        std::int16_t tick_size_decimals{};
        bool is_active{true};
    };

    struct DataSource {
        std::string code;
        std::string display_name;
        std::optional<std::string> base_url;
        bool is_active{true};
    };

    struct Quote {
        std::string symbol;
        std::string source;
        Timestamp timestamp;
        double price;
        double open;
        double high;
        double low;
        std::int64_t volume;
        std::optional<double> change_pct;
    };

    struct Tick {
        std::string symbol;
        std::string source;
        Timestamp timestamp;
        std::optional<std::string> trade_id;
        double price;
        double size;
        TradeSide side{TradeSide::unknown};
    };

    struct OrderBookSnapshot {
        std::string symbol;
        std::string source;
        Timestamp snapshot_time;
        double best_bid;
        double best_ask;
        double imbalance;
        std::int64_t total_bid_volume;
        std::int64_t total_ask_volume;
    };

    struct TechnicalIndicator {
        std::string symbol;
        std::string source;
        std::string indicator_name;
        Timestamp timestamp;
        double value;
        std::string parameters_json{"{}"};
    };

    [[nodiscard]] std::string_view to_string(AssetClass value) noexcept;
    [[nodiscard]] char to_char(TradeSide value) noexcept;
}
