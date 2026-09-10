#pragma once

#include "config.hpp"
#include "html_parser.hpp"

#include "messaging/amqp_service.hpp"

#include <string>

namespace habr::services::parser {

class ParserService : public shared::messaging::AmqpService {
  public:
    explicit ParserService(Config config);

  protected:
    void OnChannelReady() override;

  private:
    shared::messaging::Ack HandleRawArticles(const std::string& payload);

    Config config_;
    HtmlParser html_parser_;
};

} // namespace habr::services::parser
