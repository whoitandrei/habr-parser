#include "common/time_util.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace habr::shared::common {

std::string NowIso8601Utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    std::tm utc{};
    gmtime_r(&now_c, &utc);

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string GenerateTraceId() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(8) << dist(rng);
    return oss.str();
}

} // namespace habr::shared::common
