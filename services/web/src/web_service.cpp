#include "web_service.hpp"

#include "html_view.hpp"

#include "common/log.hpp"

#include <nlohmann/json.hpp>

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <stdexcept>

namespace habr::services::web {

using shared::common::LogError;
using shared::common::LogInfo;
using shared::messaging::Ack;
using shared::messaging::DigestReadyMessage;
using shared::messaging::kDigestReadyExchange;
using shared::messaging::kDigestWebQueue;

WebService::WebService(Config config) : AmqpService("web", config.amqp), config_(std::move(config)) {
    StartHttpServer();
}

WebService::~WebService() {
    if (http_ != nullptr) {
        evhttp_free(http_);
    }
}

void WebService::StartHttpServer() {
    http_ = evhttp_new(loop());
    if (http_ == nullptr) {
        throw std::runtime_error("failed to create the evhttp server");
    }

    evhttp_set_allowed_methods(http_, EVHTTP_REQ_GET);
    evhttp_set_gencb(http_, &WebService::OnHttpRequestTrampoline, this);

    if (evhttp_bind_socket(http_, config_.http_bind.c_str(), config_.http_port) != 0) {
        throw std::runtime_error("cannot bind " + config_.http_bind + ":" +
                                 std::to_string(config_.http_port));
    }

    LogInfo() << "HTTP listening on " << config_.http_bind << ":" << config_.http_port;
}

void WebService::OnChannelReady() {
    DeclareFanoutExchange(kDigestReadyExchange);
    DeclareQueue(kDigestWebQueue);
    BindQueue(kDigestWebQueue, kDigestReadyExchange);

    Consume(
        kDigestWebQueue,
        [this](const std::string& payload) { return HandleDigestReady(payload); },
        config_.prefetch);
}

Ack WebService::HandleDigestReady(const std::string& payload) {
    DigestReadyMessage digest;
    try {
        digest = nlohmann::json::parse(payload).get<DigestReadyMessage>();
    } catch (const std::exception& e) {
        LogError() << "Malformed digest_ready message dropped: " << e.what();
        return Ack::kReject;
    }

    LogInfo() << "Digest received: " << digest.articles.size() << " articles (trace_id="
              << digest.trace_id << ")";

    digests_.push_front(std::move(digest));
    while (digests_.size() > config_.history_size) {
        digests_.pop_back();
    }

    return Ack::kAck;
}

void WebService::OnHttpRequestTrampoline(evhttp_request* request, void* ctx) {
    static_cast<WebService*>(ctx)->HandleHttpRequest(request);
}

void WebService::HandleHttpRequest(evhttp_request* request) {
    const evhttp_uri* uri = evhttp_request_get_evhttp_uri(request);
    const char* raw_path = (uri != nullptr) ? evhttp_uri_get_path(uri) : nullptr;
    const std::string path = (raw_path != nullptr && raw_path[0] != '\0') ? raw_path : "/";

    int status = 200;
    const char* reason = "OK";
    std::string content_type;
    std::string body;

    if (path == "/" || path == "/index.html") {
        content_type = "text/html; charset=utf-8";
        body = RenderPage(digests_);
    } else if (path == "/api/digest") {
        content_type = "application/json; charset=utf-8";
        body = digests_.empty() ? std::string("{}") : nlohmann::json(digests_.front()).dump(2);
    } else if (path == "/healthz") {
        content_type = "text/plain; charset=utf-8";
        // Reports the broker link, not just "the process is running" — a web
        // service that is up but deaf is the failure worth catching.
        body = connected() ? "ok\n" : "degraded: no broker connection\n";
    } else {
        status = 404;
        reason = "Not Found";
        content_type = "text/plain; charset=utf-8";
        body = "not found\n";
    }

    evkeyvalq* headers = evhttp_request_get_output_headers(request);
    evhttp_add_header(headers, "Content-Type", content_type.c_str());
    evhttp_add_header(headers, "Cache-Control", "no-store");

    evbuffer* out = evhttp_request_get_output_buffer(request);
    evbuffer_add(out, body.data(), body.size());
    evhttp_send_reply(request, status, reason, out);
}

} // namespace habr::services::web
