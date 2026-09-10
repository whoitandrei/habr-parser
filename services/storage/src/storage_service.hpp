#pragma once

#include "article_store.hpp"
#include "config.hpp"

#include "messaging/amqp_service.hpp"

#include <string>

namespace habr::services::storage {

class StorageService : public shared::messaging::AmqpService {
  public:
    explicit StorageService(Config config);

  protected:
    void OnChannelReady() override;
    void OnTick() override;

  private:
    shared::messaging::Ack HandleParsedArticles(const std::string& payload);
    void PublishDigest();

    Config config_;
    ArticleStore store_;
};

} // namespace habr::services::storage
