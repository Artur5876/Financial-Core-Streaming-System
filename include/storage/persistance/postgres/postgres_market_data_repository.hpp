#pragma once

#include "storage/persistance/market_data_repository.hpp"
#include "storage/persistance/postgres/postgres_market_data_repository.hpp"

#include <memory>

namespace fincore::persistence::postgres {
    // One repository owns one PostgreSQL connection and is
    // intentionally not thread-safe
    // It gives each database writing its own repository instanced

    class PostgresMarketDataRepository final : public MarketDataRepository {
        public:
            explicit PostgresMarketDataRepository(ConnectionConfig config);
            explicit PostgresMarketDataRepository(std::unique_ptr<PgConnection> connection);

            PostgresMarketDataRepository(const PostgresMarketDataRepository&) = delete;
            PostgresMarketDataRepository& operator=(const PostgresMarketDataRepository&) = delete;

            void upsert_instrument(const domain::Instrument& instrument) override;
            void upsert_data_source(const domain::DataSource& source) override;

            InsertSummary insert_quote(std::span<const domain::Quote> quotes) override;
            InsertSummary insert_ticks(std::span<const domain::Tick> ticks) override;
            InsertSummary insert_order_book_snapshots(std::span<const domain::OrderBookSnapshot> snapshots) override;
            InsertSummary insert_technical_indicators(std::span<const domain::TechnicalIndicator> indicators) override;

            [[nodiscard]] std::optional<domain::Quote> latest_quote(std::string_view symbol, std::string_view source) override;

        private:
            void prepare_statements();

            std::unique_ptr<PgConnection> connection_;
    };
}
