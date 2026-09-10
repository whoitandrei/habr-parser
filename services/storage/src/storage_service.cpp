#include "storage_service.hpp"

#include "common/log.hpp"
#include "common/time_util.hpp"
#include "messaging/messages.hpp"

#include <nlohmann/json.hpp>

namespace habr::services::storage {

using shared::common::LogError;
using shared::common::LogInfo;
using shared::common::LogWarn;
using shared::messaging::Ack;
using shared::messaging::DigestArticle;
using shared::messaging::DigestReadyMessage;
using shared::messaging::kDigestReadyExchange;
using shared::messaging::kParsedArticlesQueue;
using shared::messaging::ParsedArticlesMessage;

StorageService::StorageService(Config config)
    : AmqpService("storage", config.amqp), config_(std::move(config)),
      store_(config_.database_path) {}

void StorageService::OnChannelReady() {
    DeclareQueue(kParsedArticlesQueue);
    DeclareFanoutExchange(kDigestReadyExchange);

    Consume(
        kParsedArticlesQueue,
        [this](const std::string& payload) { return HandleParsedArticles(payload); },
        config_.prefetch);

    LogInfo() << "Digest every " << config_.digest_interval_seconds << "s: top "
              << config_.digest_top_n << " over the last " << config_.digest_period_hours << "h";

    StartTimer(config_.digest_interval_seconds, /*fire_immediately=*/false);
}

Ack StorageService::HandleParsedArticles(const std::string& payload) {
    ParsedArticlesMessage parsed;
    try {
        parsed = nlohmann::json::parse(payload).get<ParsedArticlesMessage>();
    } catch (const std::exception& e) {
        LogError() << "Malformed parsed_articles message dropped: " << e.what();
        return Ack::kReject;
    }

    try {
        const size_t written = store_.UpsertBatch(parsed.articles, parsed.trace_id);
        LogInfo() << "Stored " << written << " articles (trace_id=" << parsed.trace_id << ", total "
                  << store_.CountArticles() << ")";
    } catch (const std::exception& e) {
        LogError() << "Database write failed (trace_id=" << parsed.trace_id
                   << "), requeueing: " << e.what();
        return Ack::kRequeue;
    }

    return Ack::kAck;
}

void StorageService::OnTick() {
    PublishDigest();
}

void StorageService::PublishDigest() {
    std::vector<DigestArticle> top;
    try {
        top =
            store_.TopArticles(config_.digest_period_hours, config_.digest_top_n, config_.weights);
    } catch (const std::exception& e) {
        LogError() << "Could not build the digest: " << e.what();
        return;
    }

    if (top.empty()) {
        LogInfo() << "No articles in the last " << config_.digest_period_hours
                  << "h; skipping this digest";
        return;
    }

    DigestReadyMessage digest;
    digest.digest_id = shared::common::NowIso8601Utc();
    digest.generated_at = digest.digest_id;
    digest.trace_id = shared::common::GenerateTraceId();
    digest.period_hours = config_.digest_period_hours;
    digest.articles = std::move(top);

    const std::string out = nlohmann::json(digest).dump();

    if (!PublishToExchange(kDigestReadyExchange, out)) {
        LogWarn() << "Could not publish the digest (trace_id=" << digest.trace_id
                  << "); it will be rebuilt on the next tick";
        return;
    }

    LogInfo() << "Published digest of " << digest.articles.size() << " articles to \""
              << kDigestReadyExchange << "\" (trace_id=" << digest.trace_id << ")";
}

} // namespace habr::services::storage
