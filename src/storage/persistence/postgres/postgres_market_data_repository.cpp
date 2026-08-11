#include "storage/persistance/postgres/postgres_market_data_repository.hpp"

#include <vector>
#include <chrono>
#include <string>

namespace fincore::persistence::postgres {
    namespace {
        using domain::Decimal;
        using domain::Timestamp;

        [[nodiscard]] std::string timestamp_to_utc_text(Timestamp timestamp) {
            using namespace std::chrono;

            const auto micros = floor<microseconds>(timestamp);
            const auto seconds_part = floor<seconds>(micros);
            const auto fractional = duration_cast<microseconds>(micros - seconds_part).count();
            const std::time_t time = system_clock::

        }

    }
}


