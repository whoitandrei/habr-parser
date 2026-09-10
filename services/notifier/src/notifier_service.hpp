#pragma once

#include "config.hpp"

#include "messaging/amqp_service.hpp"

#include <string>

namespace habr::services::notifier {

class NotifierService : public shared::messaging::AmqpService {
  public:
    explicit NotifierService(Config config);

  protected:
    void OnChannelReady() override;

  private:
    shared::messaging::Ack HandleDigestReady(const std::string& payload);

    Config config_;
};

} // namespace habr::services::notifier
