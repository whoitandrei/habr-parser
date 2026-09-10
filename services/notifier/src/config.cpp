#include "config.hpp"

#include <cstdlib>

namespace habr::services::notifier {

Config LoadConfigFromEnv() {
    Config config;
    config.amqp = shared::messaging::LoadAmqpConfigFromEnv();

    if (const char* dir = std::getenv("DIGEST_OUTPUT_DIR")) {
        config.output_dir = dir;
    }

    return config;
}

} // namespace habr::services::notifier
