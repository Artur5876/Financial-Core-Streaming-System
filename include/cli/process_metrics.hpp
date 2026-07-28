#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace fincore::cli {

struct ProcessSnapshot {
    std::int64_t user_cpu_us{};
    std::int64_t system_cpu_us{};
    std::int64_t current_rss_kb{};
    std::int64_t peak_rss_kb{};
    std::int64_t minor_faults{};
    std::int64_t major_faults{};
    std::int64_t voluntary_context_switches{};
    std::int64_t involuntary_context_switches{};
};

struct ProcessDelta {
    std::int64_t user_cpu_us{};
    std::int64_t system_cpu_us{};
    std::int64_t rss_delta_kb{};
    std::int64_t current_rss_kb{};
    std::int64_t peak_rss_kb{};
    std::int64_t minor_faults{};
    std::int64_t major_faults{};
    std::int64_t voluntary_context_switches{};
    std::int64_t involuntary_context_switches{};

    [[nodiscard]] std::int64_t total_cpu_us() const noexcept {
        return user_cpu_us + system_cpu_us;
    }

    [[nodiscard]] double cpu_percent(std::int64_t wall_time_us) const noexcept;
};

[[nodiscard]] ProcessSnapshot read_process_snapshot();
[[nodiscard]] ProcessDelta process_delta(const ProcessSnapshot& before,
                                         const ProcessSnapshot& after);

struct OperationMetrics {
    std::string symbol;
    bool success{false};
    bool api_cached{false};
    bool redis_cache_hit{false};
    bool redis_quote_stored{false};
    bool redis_book_stored{false};

    std::optional<std::int64_t> api_us;
    std::optional<std::int64_t> redis_read_us;
    std::optional<std::int64_t> redis_quote_us;
    std::optional<std::int64_t> book_build_us;
    std::optional<std::int64_t> redis_book_us;
    std::int64_t total_us{};

    ProcessDelta process;
};

class SessionStats {
public:
    void record(const OperationMetrics& metrics);
    void reset();

    [[nodiscard]] const std::optional<OperationMetrics>& last() const noexcept {
        return last_;
    }

    void print_summary(std::ostream& out) const;
    void print_last(std::ostream& out) const;

private:
    struct LatencySeries {
        std::vector<std::int64_t> samples_us;

        void add(std::optional<std::int64_t> sample);
        void add(std::int64_t sample);
        void clear();
        void print(std::ostream& out, const char* name) const;
    };

    std::uint64_t attempts_{};
    std::uint64_t successes_{};
    std::uint64_t failures_{};
    std::uint64_t api_cache_hits_{};
    std::uint64_t api_cache_misses_{};
    std::uint64_t redis_cache_hits_{};
    std::uint64_t redis_cache_misses_{};
    std::uint64_t redis_quote_successes_{};
    std::uint64_t redis_quote_failures_{};
    std::uint64_t redis_book_successes_{};
    std::uint64_t redis_book_failures_{};

    std::int64_t measured_user_cpu_us_{};
    std::int64_t measured_system_cpu_us_{};
    std::int64_t last_current_rss_kb_{};
    std::int64_t peak_rss_kb_{};

    LatencySeries api_;
    LatencySeries redis_read_;
    LatencySeries api_cached_;
    LatencySeries api_external_;
    LatencySeries redis_quote_;
    LatencySeries book_build_;
    LatencySeries redis_book_;
    LatencySeries total_;

    std::optional<OperationMetrics> last_;
};

void print_operation_metrics(std::ostream& out,
                             const OperationMetrics& metrics);

} // namespace fincore::cli
