#include "notifier_service.hpp"

#include "digest_writer.hpp"

#include "common/log.hpp"
#include "messaging/messages.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

namespace habr::services::notifier {

using shared::common::LogError;
using shared::common::LogInfo;
using shared::messaging::Ack;
using shared::messaging::DigestReadyMessage;
using shared::messaging::kDigestNotifierQueue;
using shared::messaging::kDigestReadyExchange;

namespace {

std::string ToFileNameToken(const std::string& value) {
    std::string token;
    token.reserve(value.size());
    for (const char ch : value) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        token.push_back(safe ? ch : '-');
    }
    if (token.empty()) {
        token = "digest";
    }
    return token;
}

} // namespace

NotifierService::NotifierService(Config config)
    : AmqpService("notifier", config.amqp), config_(std::move(config)) {}

void NotifierService::OnChannelReady() {
    DeclareFanoutExchange(kDigestReadyExchange);
    DeclareQueue(kDigestNotifierQueue);
    BindQueue(kDigestNotifierQueue, kDigestReadyExchange);

    std::error_code error;
    std::filesystem::create_directories(config_.output_dir, error);
    if (error) {
        LogError() << "Cannot create " << config_.output_dir << ": " << error.message();
    } else {
        LogInfo() << "Writing digests to " << config_.output_dir;
    }

    Consume(
        kDigestNotifierQueue,
        [this](const std::string& payload) { return HandleDigestReady(payload); },
        config_.prefetch);
}

Ack NotifierService::HandleDigestReady(const std::string& payload) {
    DigestReadyMessage digest;
    try {
        digest = nlohmann::json::parse(payload).get<DigestReadyMessage>();
    } catch (const std::exception& e) {
        LogError() << "Malformed digest_ready message dropped: " << e.what();
        return Ack::kReject;
    }

    const std::string markdown = RenderDigestMarkdown(digest);

    const std::filesystem::path directory(config_.output_dir);
    const std::filesystem::path file =
        directory / ("digest-" + ToFileNameToken(digest.digest_id) + ".md");
    const std::filesystem::path latest = directory / "latest.md";

    try {
        WriteFileAtomically(file.string(), markdown);
        WriteFileAtomically(latest.string(), markdown);
    } catch (const std::exception& e) {
        LogError() << "Could not write the digest (trace_id=" << digest.trace_id
                   << "), requeueing: " << e.what();
        return Ack::kRequeue;
    }

    LogInfo() << "Digest written to " << file.string() << " (" << digest.articles.size()
              << " articles, trace_id=" << digest.trace_id << ")";

    const size_t preview = std::min<size_t>(digest.articles.size(), 5);
    for (size_t i = 0; i < preview; ++i) {
        LogInfo() << "  " << (i + 1) << ". " << digest.articles[i].title << " — "
                  << digest.articles[i].url;
    }

    return Ack::kAck;
}

} // namespace habr::services::notifier
