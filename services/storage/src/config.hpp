#pragma once

#include "messaging/amqp_service.hpp"

#include <cstdint>
#include <string>

namespace habr::services::storage {

struct ScoreWeights {
    double votes = 10.0;
    double bookmarks = 5.0;
    double comments = 2.0;
    double views = 0.005;
};

struct Config {
    shared::messaging::AmqpConfig amqp;

    std::string database_path = "/data/habr.db";

    uint32_t digest_interval_seconds = 120;
    
    uint32_t digest_period_hours = 24;

    uint32_t digest_top_n = 10;

    ScoreWeights weights;

    uint16_t prefetch = 1;
};

Config LoadConfigFromEnv();

} // namespace habr::services::storage
