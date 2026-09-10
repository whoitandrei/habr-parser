#include "config.hpp"

#include "common/log.hpp"

#include <cstdlib>

namespace habr::services::fetcher {

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

} // namespace

Config LoadConfigFromEnv() {
    Config config;
    config.amqp = shared::messaging::LoadAmqpConfigFromEnv();

    if (const char* url = std::getenv("HABR_URL")) {
        config.habr_url = url;
    }
    if (const char* agent = std::getenv("HTTP_USER_AGENT")) {
        config.user_agent = agent;
    }

    config.interval_seconds = ReadUint32Env("FETCH_INTERVAL_SECONDS", config.interval_seconds);
    config.fetch_timeout_seconds =
        ReadUint32Env("FETCH_TIMEOUT_SECONDS", config.fetch_timeout_seconds);

    if (config.interval_seconds == 0) {
        LogWarn() << "FETCH_INTERVAL_SECONDS=0 would busy-loop the event base; using 300";
        config.interval_seconds = 300;
    }
    if (config.fetch_timeout_seconds == 0) {
        LogWarn() << "FETCH_TIMEOUT_SECONDS=0 means 'no timeout' in libcurl; using 10";
        config.fetch_timeout_seconds = 10;
    }

    return config;
}

} // namespace habr::services::fetcher
