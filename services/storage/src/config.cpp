#include "config.hpp"

#include "common/log.hpp"

#include <cstdlib>

namespace habr::services::storage {

namespace {

using shared::common::LogWarn;

uint32_t ReadUint32Env(const char* name, uint32_t fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return fallback;
    }
    try {
        return static_cast<uint32_t>(std::stoul(raw));
    } catch (const std::exception& e) {
        LogWarn() << "Invalid " << name << "=\"" << raw << "\" (" << e.what() << "), using "
                  << fallback;
        return fallback;
    }
}

double ReadDoubleEnv(const char* name, double fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return fallback;
    }
    try {
        return std::stod(raw);
    } catch (const std::exception& e) {
        LogWarn() << "Invalid " << name << "=\"" << raw << "\" (" << e.what() << "), using "
                  << fallback;
        return fallback;
    }
}

} // namespace

Config LoadConfigFromEnv() {
    Config config;
    config.amqp = shared::messaging::LoadAmqpConfigFromEnv();

    if (const char* path = std::getenv("DATABASE_PATH")) {
        config.database_path = path;
    }

    config.digest_interval_seconds =
        ReadUint32Env("DIGEST_INTERVAL_SECONDS", config.digest_interval_seconds);
    config.digest_period_hours = ReadUint32Env("DIGEST_PERIOD_HOURS", config.digest_period_hours);
    config.digest_top_n = ReadUint32Env("DIGEST_TOP_N", config.digest_top_n);

    config.weights.votes = ReadDoubleEnv("DIGEST_WEIGHT_VOTES", config.weights.votes);
    config.weights.bookmarks = ReadDoubleEnv("DIGEST_WEIGHT_BOOKMARKS", config.weights.bookmarks);
    config.weights.comments = ReadDoubleEnv("DIGEST_WEIGHT_COMMENTS", config.weights.comments);
    config.weights.views = ReadDoubleEnv("DIGEST_WEIGHT_VIEWS", config.weights.views);

    if (config.digest_interval_seconds == 0) {
        LogWarn() << "DIGEST_INTERVAL_SECONDS=0 would busy-loop the event base; using 120";
        config.digest_interval_seconds = 120;
    }
    if (config.digest_top_n == 0) {
        LogWarn() << "DIGEST_TOP_N=0 would produce empty digests; using 10";
        config.digest_top_n = 10;
    }
    if (config.digest_period_hours == 0) {
        LogWarn() << "DIGEST_PERIOD_HOURS=0 would exclude everything; using 24";
        config.digest_period_hours = 24;
    }

    return config;
}

} // namespace habr::services::storage
