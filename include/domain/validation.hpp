#pragma once

#include "domain/market_data.hpp"

namespace fincore::domain {
    void validate(const Instrument& value);
    void validate(const DataSource& value);
    void validate(const Quote& value);
    void validate(const Tick& value);
    void validate(const OrderBookSnapshot& value);
    void validate(const TechnicalIndicator& value);
}
