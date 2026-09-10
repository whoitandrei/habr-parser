#include "fetcher.hpp"

#include "common/log.hpp"
#include "common/time_util.hpp"
#include "messaging/messages.hpp"

#include <nlohmann/json.hpp>

namespace habr::services::fetcher {

using shared::common::LogError;
using shared::common::LogInfo;
using shared::messaging::kRawArticlesQueue;
using shared::messaging::RawArticlesMessage;

Fetcher::Fetcher(Config config)
    : AmqpService("fetcher", config.amqp), config_(std::move(config)),
      http_client_(config_.user_agent, config_.fetch_timeout_seconds) {}

void Fetcher::OnChannelReady() {
    DeclareQueue(kRawArticlesQueue);

    LogInfo() << "Source " << config_.habr_url << ", interval " << config_.interval_seconds
              << "s, timeout " << config_.fetch_timeout_seconds << "s";

    StartTimer(config_.interval_seconds, /*fire_immediately=*/true);
}

void Fetcher::OnTick() {
    const std::string fetch_id = shared::common::NowIso8601Utc();
    const std::string trace_id = shared::common::GenerateTraceId();

    LogInfo() << "Fetching " << config_.habr_url << " (fetch_id=" << fetch_id
              << ", trace_id=" << trace_id << ")";

    const HttpResponse response = http_client_.Get(config_.habr_url);

    if (!response.ok) {
        LogError() << "Fetch failed (trace_id=" << trace_id << "): " << response.error
                   << "; waiting for the next tick";
        return;
    }

    RawArticlesMessage message;
    message.fetch_id = fetch_id;
    message.source_url = config_.habr_url;
    message.raw_html = response.body;
    message.fetched_at = shared::common::NowIso8601Utc();
    message.trace_id = trace_id;

    const std::string payload = nlohmann::json(message).dump();

    if (Publish(kRawArticlesQueue, payload)) {
        LogInfo() << "Published " << payload.size() << " bytes to \"" << kRawArticlesQueue
                  << "\" (html " << response.body.size() << " bytes, trace_id=" << trace_id << ")";
    } else {
        LogError() << "Publish to \"" << kRawArticlesQueue << "\" rejected (trace_id=" << trace_id
                   << ")";
    }
}

} // namespace habr::services::fetcher
