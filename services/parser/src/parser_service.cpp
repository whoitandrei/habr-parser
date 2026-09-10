#include "parser_service.hpp"

#include "common/log.hpp"
#include "common/time_util.hpp"
#include "messaging/messages.hpp"

#include <nlohmann/json.hpp>

namespace habr::services::parser {

using shared::common::LogError;
using shared::common::LogInfo;
using shared::common::LogWarn;
using shared::messaging::Ack;
using shared::messaging::kParsedArticlesQueue;
using shared::messaging::kRawArticlesQueue;
using shared::messaging::ParsedArticlesMessage;
using shared::messaging::RawArticlesMessage;

ParserService::ParserService(Config config)
    : AmqpService("parser", config.amqp), config_(std::move(config)),
      html_parser_(config_.base_url) {}

void ParserService::OnChannelReady() {
    DeclareQueue(kRawArticlesQueue);
    DeclareQueue(kParsedArticlesQueue);

    Consume(
        kRawArticlesQueue,
        [this](const std::string& payload) { return HandleRawArticles(payload); },
        config_.prefetch);
}

Ack ParserService::HandleRawArticles(const std::string& payload) {
    RawArticlesMessage raw;
    try {
        raw = nlohmann::json::parse(payload).get<RawArticlesMessage>();
    } catch (const std::exception& e) {
        LogError() << "Malformed raw_articles message dropped: " << e.what();
        return Ack::kReject;
    }

    const ParseResult result = html_parser_.Parse(raw.raw_html);

    if (result.articles.empty()) {
        LogError() << "No articles found in " << raw.raw_html.size()
                   << " bytes of HTML (trace_id=" << raw.trace_id
                   << "); Habr markup may have changed";
        return Ack::kAck;
    }

    if (result.skipped > 0) {
        LogWarn() << "Skipped " << result.skipped
                  << " article node(s) without id/title (trace_id=" << raw.trace_id << ")";
    }

    ParsedArticlesMessage parsed;
    parsed.fetch_id = raw.fetch_id;
    parsed.source_url = raw.source_url;
    parsed.parsed_at = shared::common::NowIso8601Utc();
    parsed.trace_id = raw.trace_id;
    parsed.articles = result.articles;

    const std::string out = nlohmann::json(parsed).dump();

    if (!Publish(kParsedArticlesQueue, out)) {
        LogWarn() << "Could not publish parsed articles (trace_id=" << raw.trace_id
                  << "); requeueing";
        return Ack::kRequeue;
    }

    LogInfo() << "Parsed " << result.articles.size() << " articles -> \"" << kParsedArticlesQueue
              << "\" (trace_id=" << raw.trace_id << ")";
    return Ack::kAck;
}

} // namespace habr::services::parser
