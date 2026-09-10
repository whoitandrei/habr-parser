#pragma once

#include "messaging/amqp_service.hpp"

#include <cstdint>
#include <string>

namespace habr::services::web {

struct Config {
    shared::messaging::AmqpConfig amqp;

    std::string http_bind = "0.0.0.0";
    uint16_t http_port = 8080;
    size_t history_size = 20;

    uint16_t prefetch = 1;
};

Config LoadConfigFromEnv();

} // namespace habr::services::web
