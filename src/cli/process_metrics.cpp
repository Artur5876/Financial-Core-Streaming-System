#include "cli/process_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>

namespace fincore::cli {
namespace {

std::int64_t timeval_to_us(const timeval& value) {
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000LL
         + static_cast<std::int64_t>(value.tv_usec);
}

std::int64_t read_status_kb(const char* field_name) {
    std::ifstream status{"/proc/self/status"};
    if (!status) {
        return 0;
    }

    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind(field_name, 0) != 0) {
            continue;
        }

        std::istringstream input{line.substr(std::string(field_name).size())};
        std::int64_t value_kb = 0;
        input >> value_kb;
        return value_kb;
    }

    return 0;
}

std::string format_duration(std::int64_t microseconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);

    if (microseconds >= 1'000'000) {
        out << static_cast<double>(microseconds) / 1'000'000.0 << " s";
    } else if (microseconds >= 1'000) {
        out << static_cast<double>(microseconds) / 1'000.0 << " ms";
    } else {
        out << microseconds << " us";
    }

    return out.str();
}

std::string format_memory(std::int64_t kilobytes) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << static_cast<double>(kilobytes) / 1024.0 << " MiB";
    return out.str();
}

std::int64_t percentile(std::vector<std::int64_t> values, double p) {
    if (values.empty()) {
        return 0;
    }

    std::sort(values.begin(), values.end());

    const double position = p * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));

    if (lower == upper) {
        return values[lower];
    }

    const double fraction = position - static_cast<double>(lower);
    const double interpolated =
        static_cast<double>(values[lower])
        + fraction * static_cast<double>(values[upper] - values[lower]);

    return static_cast<std::int64_t>(std::llround(interpolated));
}

void print_optional_duration(std::ostream& out,
                             const char* name,
                             const std::optional<std::int64_t>& value) {
    out << "  " << std::left << std::setw(20) << name;
    if (value) {
        out << format_duration(*value) << '\n';
    } else {
        out << "not reached\n";
    }
}

} // namespace

double ProcessDelta::cpu_percent(std::int64_t wall_time_us) const noexcept {
    if (wall_time_us <= 0) {
        return 0.0;
    }

    return static_cast<double>(total_cpu_us())
         * 100.0
         / static_cast<double>(wall_time_us);
}

ProcessSnapshot read_process_snapshot() {
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        throw std::runtime_error("getrusage(RUSAGE_SELF) failed");
    }

    ProcessSnapshot snapshot;
    snapshot.user_cpu_us = timeval_to_us(usage.ru_utime);
    snapshot.system_cpu_us = timeval_to_us(usage.ru_stime);
    snapshot.current_rss_kb = read_status_kb("VmRSS:");
    snapshot.peak_rss_kb = static_cast<std::int64_t>(usage.ru_maxrss);
    snapshot.minor_faults = usage.ru_minflt;
    snapshot.major_faults = usage.ru_majflt;
    snapshot.voluntary_context_switches = usage.ru_nvcsw;
    snapshot.involuntary_context_switches = usage.ru_nivcsw;
    return snapshot;
}

ProcessDelta process_delta(const ProcessSnapshot& before,
                           const ProcessSnapshot& after) {
    ProcessDelta delta;
    delta.user_cpu_us = after.user_cpu_us - before.user_cpu_us;
    delta.system_cpu_us = after.system_cpu_us - before.system_cpu_us;
    delta.rss_delta_kb = after.current_rss_kb - before.current_rss_kb;
    delta.current_rss_kb = after.current_rss_kb;
    delta.peak_rss_kb = after.peak_rss_kb;
    delta.minor_faults = after.minor_faults - before.minor_faults;
    delta.major_faults = after.major_faults - before.major_faults;
    delta.voluntary_context_switches =
        after.voluntary_context_switches - before.voluntary_context_switches;
    delta.involuntary_context_switches =
        after.involuntary_context_switches - before.involuntary_context_switches;
    return delta;
}

void SessionStats::LatencySeries::add(
    std::optional<std::int64_t> sample) {
    if (sample) {
        samples_us.push_back(*sample);
    }
}

void SessionStats::LatencySeries::add(std::int64_t sample) {
    samples_us.push_back(sample);
}

void SessionStats::LatencySeries::clear() {
    samples_us.clear();
}

void SessionStats::LatencySeries::print(std::ostream& out,
                                        const char* name) const {
    out << "  " << std::left << std::setw(18) << name;

    if (samples_us.empty()) {
        out << "no samples\n";
        return;
    }

    const auto [minimum, maximum] = std::minmax_element(
        samples_us.begin(), samples_us.end());

    const long double total = std::accumulate(
        samples_us.begin(),
        samples_us.end(),
        static_cast<long double>(0));

    const auto average = static_cast<std::int64_t>(
        std::llround(total / static_cast<long double>(samples_us.size())));

    out << "n=" << samples_us.size()
        << " avg=" << format_duration(average)
        << " min=" << format_duration(*minimum)
        << " p50=" << format_duration(percentile(samples_us, 0.50))
        << " p95=" << format_duration(percentile(samples_us, 0.95))
        << " p99=" << format_duration(percentile(samples_us, 0.99))
        << " max=" << format_duration(*maximum)
        << '\n';
}

void SessionStats::record(const OperationMetrics& metrics) {
    ++attempts_;

    if (metrics.success) {
        ++successes_;
    } else {
        ++failures_;
    }

    if (metrics.api_us) {
        if (metrics.api_cached) {
            ++api_cache_hits_;
        } else {
            ++api_cache_misses_;
        }
    }

    if (metrics.redis_read_us) {
        if (metrics.redis_cache_hit) {
            ++redis_cache_hits_;
        } else {
            ++redis_cache_misses_;
        }
    }

    if (metrics.redis_quote_us) {
        if (metrics.redis_quote_stored) {
            ++redis_quote_successes_;
        } else {
            ++redis_quote_failures_;
        }
    }

    if (metrics.redis_book_us) {
        if (metrics.redis_book_stored) {
            ++redis_book_successes_;
        } else {
            ++redis_book_failures_;
        }
    }

    api_.add(metrics.api_us);
    redis_read_.add(metrics.redis_read_us);
    if (metrics.api_us) {
        if (metrics.api_cached) {
            api_cached_.add(metrics.api_us);
        } else {
            api_external_.add(metrics.api_us);
        }
    }
    redis_quote_.add(metrics.redis_quote_us);
    book_build_.add(metrics.book_build_us);
    redis_book_.add(metrics.redis_book_us);
    total_.add(metrics.total_us);

    measured_user_cpu_us_ += metrics.process.user_cpu_us;
    measured_system_cpu_us_ += metrics.process.system_cpu_us;
    last_current_rss_kb_ = metrics.process.current_rss_kb;
    peak_rss_kb_ = std::max(peak_rss_kb_, metrics.process.peak_rss_kb);
    last_ = metrics;
}

void SessionStats::reset() {
    attempts_ = 0;
    successes_ = 0;
    failures_ = 0;
    api_cache_hits_ = 0;
    api_cache_misses_ = 0;
    redis_cache_hits_ = 0;
    redis_cache_misses_ = 0;
    redis_quote_successes_ = 0;
    redis_quote_failures_ = 0;
    redis_book_successes_ = 0;
    redis_book_failures_ = 0;
    measured_user_cpu_us_ = 0;
    measured_system_cpu_us_ = 0;
    last_current_rss_kb_ = 0;
    peak_rss_kb_ = 0;
    api_.clear();
    redis_read_.clear();
    api_cached_.clear();
    api_external_.clear();
    redis_quote_.clear();
    book_build_.clear();
    redis_book_.clear();
    total_.clear();
    last_.reset();
}

void SessionStats::print_summary(std::ostream& out) const {
    out << "\n=== FinCore Session Statistics ===============================\n"
        << "Operations\n"
        << "  attempts             " << attempts_ << '\n'
        << "  successes            " << successes_ << '\n'
        << "  failures              " << failures_ << '\n';

    const auto cache_total = api_cache_hits_ + api_cache_misses_;
    const double cache_hit_rate = cache_total == 0
        ? 0.0
        : static_cast<double>(api_cache_hits_) * 100.0
          / static_cast<double>(cache_total);

    out << "\nAlpha Vantage client\n"
        << "  cache hits           " << api_cache_hits_ << '\n'
        << "  cache misses         " << api_cache_misses_ << '\n'
        << "  cache hit rate       " << std::fixed << std::setprecision(2)
        << cache_hit_rate << "%\n";

    out << "\nRedis\n"
        << "  cache hits           " << redis_cache_hits_ << '\n'
        << "  cache misses         " << redis_cache_misses_ << '\n'
        << "  quote writes ok      " << redis_quote_successes_ << '\n'
        << "  quote writes failed  " << redis_quote_failures_ << '\n'
        << "  book writes ok       " << redis_book_successes_ << '\n'
        << "  book writes failed   " << redis_book_failures_ << '\n';

    out << "\nLatency\n";
    redis_read_.print(out, "Redis cache read");
    api_.print(out, "API get_quote");
    api_external_.print(out, "  API external");
    api_cached_.print(out, "  API cached");
    redis_quote_.print(out, "Redis quote");
    book_build_.print(out, "Book rebuild");
    redis_book_.print(out, "Redis order book");
    total_.print(out, "Total pipeline");

    out << "\nMeasured process usage\n"
        << "  user CPU             " << format_duration(measured_user_cpu_us_) << '\n'
        << "  system CPU           " << format_duration(measured_system_cpu_us_) << '\n'
        << "  total CPU            "
        << format_duration(measured_user_cpu_us_ + measured_system_cpu_us_) << '\n'
        << "  current RSS          " << format_memory(last_current_rss_kb_) << '\n'
        << "  peak RSS             " << format_memory(peak_rss_kb_) << '\n'
        << "==============================================================\n";
}

void SessionStats::print_last(std::ostream& out) const {
    if (!last_) {
        out << "No measured operation yet.\n";
        return;
    }

    print_operation_metrics(out, *last_);
}

void print_operation_metrics(std::ostream& out,
                             const OperationMetrics& metrics) {
    out << "\n[metrics] " << metrics.symbol
        << (metrics.success ? " SUCCESS" : " FAILED")
        << '\n';

    print_optional_duration(out, "API get_quote", metrics.api_us);
    print_optional_duration(out, "Redis cache read", metrics.redis_read_us);
    print_optional_duration(out, "Redis quote write", metrics.redis_quote_us);
    print_optional_duration(out, "OrderBook rebuild", metrics.book_build_us);
    print_optional_duration(out, "Redis book write", metrics.redis_book_us);

    out << "  " << std::left << std::setw(20) << "Total pipeline"
        << format_duration(metrics.total_us) << '\n';

    if (metrics.total_us > 0) {
        out << "  " << std::left << std::setw(20) << "Pipeline rate"
            << std::fixed << std::setprecision(2)
            << 1'000'000.0 / static_cast<double>(metrics.total_us)
            << " ops/s (single operation)\n";
    }

    if (metrics.api_us) {
        out << "  " << std::left << std::setw(20) << "API source"
            << (metrics.api_cached ? "AlphaVantageClient cache" : "external fetch")
            << '\n';
    }
    if (metrics.redis_read_us) {
        out << "  " << std::left << std::setw(20) << "Redis cache"
            << (metrics.redis_cache_hit ? "hit" : "miss")
            << '\n';
    }

    out << "\n  Process delta\n"
        << "    user CPU           " << format_duration(metrics.process.user_cpu_us) << '\n'
        << "    system CPU         " << format_duration(metrics.process.system_cpu_us) << '\n'
        << "    process CPU / wall " << std::fixed << std::setprecision(2)
        << metrics.process.cpu_percent(metrics.total_us) << "%\n"
        << "    current RSS        " << format_memory(metrics.process.current_rss_kb) << '\n'
        << "    RSS delta          "
        << (metrics.process.rss_delta_kb >= 0 ? "+" : "")
        << format_memory(metrics.process.rss_delta_kb) << '\n'
        << "    peak RSS           " << format_memory(metrics.process.peak_rss_kb) << '\n'
        << "    minor faults       " << metrics.process.minor_faults << '\n'
        << "    major faults       " << metrics.process.major_faults << '\n'
        << "    voluntary ctx sw   " << metrics.process.voluntary_context_switches << '\n'
        << "    involuntary ctx sw " << metrics.process.involuntary_context_switches << '\n';
}

} // namespace fincore::cli
