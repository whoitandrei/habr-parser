#include "config.hpp"

#include "common/log.hpp"

#include <cstdlib>

namespace habr::services::web {

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

    if (const char* bind = std::getenv("HTTP_BIND")) {
        config.http_bind = bind;
    }

    const uint32_t port = ReadUint32Env("HTTP_PORT", config.http_port);
    if (port == 0 || port > 65535) {
        LogWarn() << "HTTP_PORT=" << port << " is out of range; using " << config.http_port;
    } else {
        config.http_port = static_cast<uint16_t>(port);
    }

    config.history_size = ReadUint32Env("DIGEST_HISTORY_SIZE",
                                        static_cast<uint32_t>(config.history_size));
    if (config.history_size == 0) {
        LogWarn() << "DIGEST_HISTORY_SIZE=0 would show nothing; using 20";
        config.history_size = 20;
    }

    return config;
}

} // namespace habr::services::web
