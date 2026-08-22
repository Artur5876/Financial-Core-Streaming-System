#pragma once

#include "domain/market_data.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>


namespace fincore::persistence {

    struct InsertSummary {
        std::size_t attempted{};
        std::size_t inserted{};

        //difference - > record duplication
        [[nodiscard]] std::size_t duplicates() const noexcept {
            return attempted - inserted;
        }
    };

    //Persistence port used be Fincore services
    //The domain and ingestion layer depends on this interface(not libpq)
    class MarketDataRepository {
        public:
            virtual ~MarketDataRepository() = default;

            virtual void upsert_instrument(const domain::Instrument& instrument) = 0;
            virtual void upsert_data_source(const domain::DataSource& source) = 0;

            virtual InsertSummary insert_quotes(std::span<const domain::Quote> quote) = 0;
            virtual InsertSummary insert_ticks(std::span<const domain::Tick> ticks) = 0;
            virtual InsertSummary insert_order_book_snapshots(
                std::span<const domain::OrderBookSnapshot> snapshots) = 0;
            virtual InsertSummary insert_technical_indicators(
                std::span<const domain::TechnicalIndicator> indicators) = 0;

            [[nodiscard]] virtual std::optional<domain::Quote> latest_quote(
                std::string_view symbol,
                std::string_view source) = 0;
    };

}
