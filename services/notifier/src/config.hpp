#pragma once

#include "messaging/amqp_service.hpp"

#include <cstdint>
#include <string>

namespace habr::services::notifier {

struct Config {
    shared::messaging::AmqpConfig amqp;

    std::string output_dir = "/data/digests";

    uint16_t prefetch = 1;
};

Config LoadConfigFromEnv();

} // namespace habr::services::notifier
