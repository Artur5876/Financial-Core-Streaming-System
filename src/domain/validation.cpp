#include "storage/domain/validation.hpp"

#include <string_view>
#include <stdexcept>
#include <algorithm>

namespace fincore::domain {

    //Function helpers for other function helpers
    namespace {

        [[nodiscard]] bool is_upper_identifier(std::string_view value,
                                            std::string_view extra_characters) {
            //if we wont check for emptiness that it will always be true with std::all_of
            if (value.empty()) {
                return false;
            }

            return std::all_of(value.begin(), value.end(), [extra_characters](unsigned char ch) {
                return std::isupper(ch) != 0 || std::isdigit(ch) != 0 ||
                    extra_characters.find(static_cast<char>(ch)) != std::string_view::npos;
            });
        }

        void require(bool condition, std::string_view message) {
            if (!condition) {
                throw std::invalid_argument(std::string(message));
            }
        }

    }


    // Enum converter to string type
    std::string_view to_string(AssetClass value) noexcept {
        switch (value) {
            case AssetClass::equity: return "EQUITY";
            case AssetClass::forex: return "FOREX";
            case AssetClass::crypto: return "CRYPTO";
            case AssetClass::futures: return "FUTURES";
            case AssetClass::etf: return "ETF";
        }
        return "UNKNOWN";
    }

    char to_char(TradeSide value) noexcept {
        return static_cast<char>(value);
    }

    void validate(const Instrument& value) {
    require(value.symbol.size() <= 16 && is_upper_identifier(value.symbol, "._:/-"),
            "instrument symbol must be an uppercase identifier of at most 16 characters");
    require(!value.name.empty() && value.name.size() <= 100,
            "instrument name must contain 1-100 characters");
    require(!value.exchange.empty() && value.exchange.size() <= 32,
            "exchange must contain 1-32 characters");
    require(value.tick_size_decimals >= 0 && value.tick_size_decimals <= 8,
            "tick_size_decimals must be between 0 and 8");
}

void validate(const DataSource& value) {
    require(value.code.size() <= 20 && is_upper_identifier(value.code, "_-"),
            "source code must be an uppercase identifier of at most 20 characters");
    require(!value.display_name.empty() && value.display_name.size() <= 60,
            "source display name must contain 1-60 characters");
}

void validate(const Quote& value) {
    require(!value.symbol.empty(), "quote symbol is required");
    require(!value.source.empty(), "quote source is required");
    require(value.price.raw() >= 0 && value.open.raw() >= 0 &&
                value.high.raw() >= 0 && value.low.raw() >= 0,
            "quote prices cannot be negative");
    require(value.high >= value.low, "quote high cannot be below quote low");
    require(value.volume.raw() >= 0, "quote volume cannot be negative");
}

void validate(const Tick& value) {
    require(!value.symbol.empty(), "tick symbol is required");
    require(!value.source.empty(), "tick source is required");
    require(value.price.raw() > 0, "tick price must be positive");
    require(value.size.raw() > 0, "tick size must be positive");
    require(!value.trade_id.has_value() || !value.trade_id->empty(),
            "trade_id cannot be empty when present");
}

void validate(const OrderBookSnapshot& value) {
    require(!value.symbol.empty(), "snapshot symbol is required");
    require(!value.source.empty(), "snapshot source is required");
    require(value.best_bid.raw() > 0 && value.best_ask.raw() > 0,
            "best bid and ask must be positive");
    require(value.best_ask >= value.best_bid,
            "best ask cannot be below best bid");
    require(value.imbalance.raw() >= -Decimal::scale &&
                value.imbalance.raw() <= Decimal::scale,
            "imbalance must be between -1 and 1");
    require(value.total_bid_volume.raw() >= 0 && value.total_ask_volume.raw() >= 0,
            "order-book volumes cannot be negative");
}

void validate(const TechnicalIndicator& value) {
    require(!value.symbol.empty(), "indicator symbol is required");
    require(!value.source.empty(), "indicator source is required");
    require(value.indicator_name.size() <= 32 &&
                is_upper_identifier(value.indicator_name, "_"),
            "indicator name must be an uppercase identifier of at most 32 characters");
    require(!value.parameters_json.empty(), "indicator parameters JSON cannot be empty");
}


}
