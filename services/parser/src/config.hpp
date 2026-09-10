#pragma once

#include "messaging/amqp_service.hpp"

#include <cstdint>
#include <string>

namespace habr::services::parser {

struct Config {
    shared::messaging::AmqpConfig amqp;

    std::string base_url = "https://habr.com";
    uint16_t prefetch = 1;
};

Config LoadConfigFromEnv();

} // namespace habr::services::parser
