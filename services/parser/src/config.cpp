#include "config.hpp"

#include "common/log.hpp"

#include <cstdlib>

namespace habr::services::parser {

Config LoadConfigFromEnv() {
    Config config;
    config.amqp = shared::messaging::LoadAmqpConfigFromEnv();

    if (const char* base = std::getenv("HABR_BASE_URL")) {
        config.base_url = base;
    }

    if (const char* raw = std::getenv("PARSER_PREFETCH")) {
        try {
            const unsigned long parsed = std::stoul(raw);
            if (parsed >= 1 && parsed <= 1000) {
                config.prefetch = static_cast<uint16_t>(parsed);
            } else {
                shared::common::LogWarn()
                    << "PARSER_PREFETCH=" << raw << " out of range; using " << config.prefetch;
            }
        } catch (const std::exception& e) {
            shared::common::LogWarn()
                << "Invalid PARSER_PREFETCH=\"" << raw << "\" (" << e.what() << ")";
        }
    }

    return config;
}

} // namespace habr::services::parser
