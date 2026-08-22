#pragma once

#include "storage/persistance/market_data_repository.hpp"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace fincore::cli {

class PostgresCli final {
public:
    PostgresCli(persistence::MarketDataRepository& repository,
                std::ostream& out,
                std::ostream& err);

    int run(const std::vector<std::string>& arguments);
    static void print_help(std::ostream& out);

private:
    persistence::MarketDataRepository& repository_;
    std::ostream& out_;
    std::ostream& err_;
};

} // namespace fincore::cli
