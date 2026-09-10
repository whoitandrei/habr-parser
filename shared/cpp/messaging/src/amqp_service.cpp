#include "messaging/amqp_service.hpp"

#include "common/log.hpp"

#include <csignal>
#include <cstdlib>
#include <stdexcept>

namespace habr::shared::messaging {

using common::LogError;
using common::LogInfo;
using common::LogWarn;

namespace {

// Seconds to wait for the broker to acknowledge a graceful close before the
// loop is torn down anyway. Without this a broker that stops answering would
// hang shutdown until Docker's SIGKILL.
constexpr int kForcedExitDelaySeconds = 2;

std::string ReadStringEnv(const char* name, std::string fallback) {
    if (const char* raw = std::getenv(name)) {
        return raw;
    }
    return fallback;
}

uint32_t ReadUint32Env(const char* name, uint32_t fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return fallback;
    }
    try {
        return static_cast<uint32_t>(std::stoul(raw));
    } catch (const std::exception& e) {
        LogWarn() << "Invalid " << name << "=\"" << raw << "\" (" << e.what() << "), using "
                  << fallback;
        return fallback;
    }
}

} // namespace

AmqpConfig LoadAmqpConfigFromEnv() {
    AmqpConfig config;
    config.host = ReadStringEnv("RABBITMQ_HOST", config.host);
    config.port = static_cast<uint16_t>(ReadUint32Env("RABBITMQ_PORT", config.port));
    config.user = ReadStringEnv("RABBITMQ_USER", config.user);
    config.password = ReadStringEnv("RABBITMQ_PASSWORD", config.password);
    config.vhost = ReadStringEnv("RABBITMQ_VHOST", config.vhost);
    config.max_reconnect_attempts =
        ReadUint32Env("RABBITMQ_MAX_RECONNECT_ATTEMPTS", config.max_reconnect_attempts);
    config.reconnect_delay_seconds =
        ReadUint32Env("RABBITMQ_RECONNECT_DELAY_SECONDS", config.reconnect_delay_seconds);
    return config;
}

// ---------------------------------------------------------------------------
// Connection handler
// ---------------------------------------------------------------------------

class AmqpService::Handler : public AMQP::LibEventHandler {
  public:
    Handler(struct event_base* evbase, AmqpService* owner)
        : AMQP::LibEventHandler(evbase), owner_(owner) {}

    void onError(AMQP::TcpConnection* /*connection*/, const char* message) override {
        owner_->HandleConnectionFailure(message);
    }

    void onClosed(AMQP::TcpConnection* /*connection*/) override {
        owner_->HandleDetached();
    }

    void onLost(AMQP::TcpConnection* /*connection*/) override {
        owner_->HandleConnectionFailure("connection lost");
    }

    void onDetached(AMQP::TcpConnection* /*connection*/) override {
        owner_->HandleDetached();
    }

  private:
    AmqpService* owner_;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AmqpService::AmqpService(std::string service_name, AmqpConfig config)
    : service_name_(std::move(service_name)), config_(std::move(config)) {
    common::SetServiceName(service_name_);

    event_base_ = event_base_new();
    if (event_base_ == nullptr) {
        throw std::runtime_error("failed to create libevent event_base");
    }

    timer_event_ = event_new(event_base_, -1, EV_PERSIST, &AmqpService::OnTimerTrampoline, this);

    // Signals go through the same event loop as everything else, so shutdown
    // runs on the loop thread and never races with an in-flight AMQP callback.
    sigterm_event_ = evsignal_new(event_base_, SIGTERM, &AmqpService::OnSignalTrampoline, this);
    sigint_event_ = evsignal_new(event_base_, SIGINT, &AmqpService::OnSignalTrampoline, this);
    event_add(sigterm_event_, nullptr);
    event_add(sigint_event_, nullptr);
}

AmqpService::~AmqpService() {
    if (timer_event_ != nullptr) {
        event_free(timer_event_);
    }
    if (sigterm_event_ != nullptr) {
        event_free(sigterm_event_);
    }
    if (sigint_event_ != nullptr) {
        event_free(sigint_event_);
    }

    channel_.reset();
    connection_.reset();
    handler_.reset();

    if (event_base_ != nullptr) {
        event_base_free(event_base_);
    }
}

int AmqpService::Run() {
    LogInfo() << "Starting; broker " << config_.host << ":" << config_.port;

    Connect();
    event_base_dispatch(event_base_);

    LogInfo() << "Stopped, exit_code=" << exit_code_;
    return exit_code_;
}

std::string AmqpService::BuildAmqpAddress() const {
    // AMQP::Address treats an empty path as the default vhost "/".
    const std::string path = (config_.vhost == "/") ? "" : config_.vhost;
    return "amqp://" + config_.user + ":" + config_.password + "@" + config_.host + ":" +
           std::to_string(config_.port) + "/" + path;
}

void AmqpService::Connect() {
    channel_ready_ = false;

    handler_ = std::make_unique<Handler>(event_base_, this);

    try {
        connection_ = std::make_unique<AMQP::TcpConnection>(handler_.get(),
                                                           AMQP::Address(BuildAmqpAddress()));
    } catch (const std::exception& e) {
        HandleConnectionFailure(std::string("cannot open connection: ") + e.what());
        return;
    }

    // The channel can be created before the handshake finishes: AMQP-CPP queues
    // its frames and flushes them once the connection is ready.
    channel_ = std::make_unique<AMQP::TcpChannel>(connection_.get());
    channel_->onReady([this] { HandleConnectionReady(); });
    channel_->onError([this](const char* message) {
        HandleConnectionFailure(std::string("channel error: ") + message);
    });
}

void AmqpService::HandleConnectionReady() {
    LogInfo() << "Connected to RabbitMQ";

    channel_ready_ = true;
    reconnect_attempts_ = 0;

    // Queues, consumers and timers are (re)established here rather than in the
    // constructor, which is what makes a reconnect fully self-healing.
    OnChannelReady();
}

void AmqpService::HandleConnectionFailure(const std::string& reason) {
    if (shutting_down_) {
        return;
    }

    const bool was_connected = channel_ready_;
    channel_ready_ = false;

    LogError() << "AMQP failure: " << reason;

    if (was_connected) {
        StopTimer();
        OnConnectionLost();
    }

    ScheduleReconnect();
}

void AmqpService::HandleDetached() {
    if (shutting_down_) {
        // The broker acknowledged our close; no reason to wait for the timeout.
        event_base_loopexit(event_base_, nullptr);
    }
}

void AmqpService::ScheduleReconnect() {
    if (reconnect_scheduled_) {
        return;
    }

    ++reconnect_attempts_;
    if (reconnect_attempts_ > config_.max_reconnect_attempts) {
        LogError() << "Giving up after " << config_.max_reconnect_attempts
                   << " reconnect attempts; exiting so the orchestrator can restart us";
        exit_code_ = 1;
        shutting_down_ = true;
        StopTimer();
        event_base_loopexit(event_base_, nullptr);
        return;
    }

    LogInfo() << "Reconnecting in " << config_.reconnect_delay_seconds << "s (attempt "
              << reconnect_attempts_ << "/" << config_.max_reconnect_attempts << ")";

    reconnect_scheduled_ = true;
    struct timeval delay {};
    delay.tv_sec = static_cast<time_t>(config_.reconnect_delay_seconds);
    event_base_once(event_base_, -1, EV_TIMEOUT, &AmqpService::OnReconnectTrampoline, this, &delay);
}

void AmqpService::DoReconnect() {
    reconnect_scheduled_ = false;
    if (shutting_down_) {
        return;
    }

    // Safe to destroy the old objects here: this runs from a timer callback,
    // not from inside an AMQP-CPP callback, where destroying the connection
    // would pull the ground out from under the library.
    channel_.reset();
    connection_.reset();
    handler_.reset();

    Connect();
}

// ---------------------------------------------------------------------------
// Channel operations
// ---------------------------------------------------------------------------

void AmqpService::DeclareQueue(const std::string& queue) {
    if (!channel_) {
        return;
    }

    // Durable, so queued work survives a broker restart.
    channel_->declareQueue(queue, AMQP::durable)
        .onSuccess([queue](const std::string& /*name*/, uint32_t message_count,
                           uint32_t /*consumer_count*/) {
            LogInfo() << "Queue \"" << queue << "\" ready (" << message_count << " pending)";
        })
        .onError([queue](const char* message) {
            LogError() << "Failed to declare queue \"" << queue << "\": " << message;
        });
}

void AmqpService::DeclareFanoutExchange(const std::string& exchange) {
    if (!channel_) {
        return;
    }

    channel_->declareExchange(exchange, AMQP::fanout, AMQP::durable)
        .onSuccess([exchange]() { LogInfo() << "Fanout exchange \"" << exchange << "\" ready"; })
        .onError([exchange](const char* message) {
            LogError() << "Failed to declare exchange \"" << exchange << "\": " << message;
        });
}

void AmqpService::BindQueue(const std::string& queue, const std::string& exchange) {
    if (!channel_) {
        return;
    }

    // Empty routing key: a fanout exchange ignores it entirely.
    channel_->bindQueue(exchange, queue, "")
        .onSuccess([queue, exchange]() {
            LogInfo() << "Bound \"" << queue << "\" to exchange \"" << exchange << "\"";
        })
        .onError([queue, exchange](const char* message) {
            LogError() << "Failed to bind \"" << queue << "\" to \"" << exchange
                       << "\": " << message;
        });
}

bool AmqpService::PublishTo(const std::string& exchange, const std::string& routing_key,
                            const std::string& payload) {
    if (!channel_ || !channel_ready_) {
        LogWarn() << "Not publishing to \"" << (exchange.empty() ? routing_key : exchange)
                  << "\": channel is not ready";
        return false;
    }

    AMQP::Envelope envelope(payload.data(), payload.size());
    envelope.setContentType("application/json");
    // Persistent: combined with a durable queue, a broker restart does not lose
    // the message.
    envelope.setDeliveryMode(2);

    return channel_->publish(exchange, routing_key, envelope);
}

bool AmqpService::Publish(const std::string& queue, const std::string& payload) {
    // The default exchange routes to the queue whose name equals the routing key.
    return PublishTo("", queue, payload);
}

bool AmqpService::PublishToExchange(const std::string& exchange, const std::string& payload) {
    return PublishTo(exchange, "", payload);
}

void AmqpService::Consume(const std::string& queue, MessageHandler handler, uint16_t prefetch) {
    if (!channel_) {
        return;
    }

    // Without a prefetch limit the broker would push the whole queue at us at
    // once, which defeats manual acknowledgements as a backpressure mechanism.
    channel_->setQos(prefetch);

    auto shared_handler = std::make_shared<MessageHandler>(std::move(handler));

    channel_->consume(queue)
        .onReceived([this, shared_handler, queue](const AMQP::Message& message, uint64_t tag,
                                                  bool /*redelivered*/) {
            std::string payload(message.body(), static_cast<size_t>(message.bodySize()));

            Ack decision = Ack::kReject;
            try {
                decision = (*shared_handler)(payload);
            } catch (const std::exception& e) {
                LogError() << "Handler for \"" << queue << "\" threw: " << e.what();
                decision = Ack::kReject;
            }

            switch (decision) {
            case Ack::kAck:
                channel_->ack(tag);
                break;
            case Ack::kRequeue:
                channel_->reject(tag, AMQP::requeue);
                break;
            case Ack::kReject:
                // Dropped rather than requeued: a message we cannot parse will
                // fail identically forever and would otherwise spin the queue.
                channel_->reject(tag, 0);
                break;
            }
        })
        .onSuccess([queue]() { LogInfo() << "Consuming from \"" << queue << "\""; })
        .onError([queue](const char* message) {
            LogError() << "Failed to consume from \"" << queue << "\": " << message;
        });
}

// ---------------------------------------------------------------------------
// Timer and shutdown
// ---------------------------------------------------------------------------

void AmqpService::StartTimer(uint32_t interval_seconds, bool fire_immediately) {
    timer_interval_seconds_ = interval_seconds;
    timer_armed_ = true;

    struct timeval interval {};
    interval.tv_sec = static_cast<time_t>(interval_seconds);
    event_add(timer_event_, &interval);

    LogInfo() << "Timer armed at " << interval_seconds << "s";

    if (fire_immediately) {
        // Do the first tick promptly instead of idling for a whole interval
        // after a cold start. It is deferred to the next loop iteration rather
        // than called inline, because StartTimer runs inside an AMQP-CPP
        // callback and a tick may block (Fetcher's HTTP request does).
        struct timeval now {};
        event_base_once(event_base_, -1, EV_TIMEOUT, &AmqpService::OnTimerTrampoline, this, &now);
    }
}

void AmqpService::StopTimer() {
    if (timer_armed_) {
        event_del(timer_event_);
        timer_armed_ = false;
    }
}

void AmqpService::Stop(int exit_code) {
    if (shutting_down_) {
        return;
    }

    LogInfo() << "Shutting down gracefully";

    shutting_down_ = true;
    exit_code_ = exit_code;
    channel_ready_ = false;
    StopTimer();

    if (connection_) {
        connection_->close();

        struct timeval delay {};
        delay.tv_sec = kForcedExitDelaySeconds;
        event_base_once(event_base_, -1, EV_TIMEOUT, &AmqpService::OnForcedExitTrampoline, this,
                        &delay);
    } else {
        event_base_loopexit(event_base_, nullptr);
    }
}

// ---------------------------------------------------------------------------
// libevent trampolines (C callbacks -> member functions)
// ---------------------------------------------------------------------------

void AmqpService::OnTimerTrampoline(evutil_socket_t /*fd*/, short /*what*/, void* ctx) {
    static_cast<AmqpService*>(ctx)->OnTick();
}

void AmqpService::OnSignalTrampoline(evutil_socket_t signum, short /*what*/, void* ctx) {
    LogInfo() << "Received signal " << signum;
    static_cast<AmqpService*>(ctx)->Stop(0);
}

void AmqpService::OnReconnectTrampoline(evutil_socket_t /*fd*/, short /*what*/, void* ctx) {
    static_cast<AmqpService*>(ctx)->DoReconnect();
}

void AmqpService::OnForcedExitTrampoline(evutil_socket_t /*fd*/, short /*what*/, void* ctx) {
    auto* self = static_cast<AmqpService*>(ctx);
    LogWarn() << "Broker did not confirm close in time; exiting anyway";
    event_base_loopexit(self->event_base_, nullptr);
}

} // namespace habr::shared::messaging
