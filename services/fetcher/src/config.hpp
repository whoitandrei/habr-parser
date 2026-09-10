#pragma once

#include "messaging/amqp_service.hpp"

#include <cstdint>
#include <string>

namespace habr::services::fetcher {

struct Config {
    shared::messaging::AmqpConfig amqp;

    std::string habr_url = "https://habr.com/ru/articles/top/daily/";

    uint32_t interval_seconds = 300;

    uint32_t fetch_timeout_seconds = 10;

    std::string user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                             "AppleWebKit/537.36 (KHTML, like Gecko) "
                             "Chrome/128.0.0.0 Safari/537.36";
};

Config LoadConfigFromEnv();

} // namespace habr::services::fetcher
