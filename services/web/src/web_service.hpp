#pragma once

#include "config.hpp"

#include "messaging/amqp_service.hpp"
#include "messaging/messages.hpp"

#include <event2/http.h>

#include <deque>
#include <string>

namespace habr::services::web {

class WebService : public shared::messaging::AmqpService {
  public:
    explicit WebService(Config config);
    ~WebService() override;

  protected:
    void OnChannelReady() override;

  private:
    shared::messaging::Ack HandleDigestReady(const std::string& payload);

    void StartHttpServer();
    void HandleHttpRequest(evhttp_request* request);
    static void OnHttpRequestTrampoline(evhttp_request* request, void* ctx);

    Config config_;
    std::deque<shared::messaging::DigestReadyMessage> digests_;
    evhttp* http_ = nullptr;
};

} // namespace habr::services::web
