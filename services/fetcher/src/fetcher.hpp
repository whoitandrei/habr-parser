#pragma once

#include "config.hpp"
#include "http_client.hpp"

#include "messaging/amqp_service.hpp"

namespace habr::services::fetcher {

class Fetcher : public shared::messaging::AmqpService {
  public:
    explicit Fetcher(Config config);

  protected:
    void OnChannelReady() override;
    void OnTick() override;

  private:
    Config config_;
    HttpClient http_client_;
};

} // namespace habr::services::fetcher
