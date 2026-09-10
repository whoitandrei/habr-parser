#pragma once

#include <amqpcpp.h>
#include <amqpcpp/libevent.h>
#include <event2/event.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace habr::shared::messaging {

struct AmqpConfig {
    std::string host = "localhost";
    uint16_t port = 5672;
    std::string user = "guest";
    std::string password = "guest";
    std::string vhost = "/";

    // Reconnect policy. Fixed delay rather than exponential backoff: RabbitMQ
    // is a local infrastructure dependency here, not a rate-limited third
    // party, so the useful behaviour is "keep trying promptly, then die and let
    // the orchestrator restart us".
    uint32_t max_reconnect_attempts = 10;
    uint32_t reconnect_delay_seconds = 3;
};

AmqpConfig LoadAmqpConfigFromEnv();

// What a consumer callback decides to do with a delivery.
enum class Ack {
    kAck,     // handled successfully
    kReject,  // permanently broken message — drop it (no requeue)
    kRequeue, // transient failure — put it back for another attempt
};

using MessageHandler = std::function<Ack(const std::string& payload)>;

// Every service in this project has the same skeleton: exactly one libevent
// event_base drives the AMQP socket, the SIGTERM/SIGINT handlers and an
// optional periodic timer. AmqpService implements that skeleton once so the
// four services only differ in what they do on a tick or on a message.
class AmqpService {
  public:
    AmqpService(std::string service_name, AmqpConfig config);
    virtual ~AmqpService();

    AmqpService(const AmqpService&) = delete;
    AmqpService& operator=(const AmqpService&) = delete;

    // Runs the event loop until a signal arrives or reconnects are exhausted.
    // Returns the process exit code.
    int Run();

  protected:
    // Called once per successful (re)connect, when the channel is usable.
    // Declare queues, start consuming and arm timers here — doing it here
    // rather than in the constructor is what makes reconnects self-healing.
    virtual void OnChannelReady() = 0;

    // Called when a working connection drops, before a reconnect is scheduled.
    virtual void OnConnectionLost() {}

    // Called on every periodic timer tick, if the subclass armed one.
    virtual void OnTick() {}

    // Declares a durable queue, so messages survive a broker restart.
    void DeclareQueue(const std::string& queue);

    // Declares a durable fanout exchange — every bound queue receives a copy of
    // every message. Use this instead of a shared queue when several services
    // must each see all of the traffic.
    void DeclareFanoutExchange(const std::string& exchange);

    // Binds a queue to a fanout exchange. Fanout ignores routing keys, so none
    // is taken.
    void BindQueue(const std::string& queue, const std::string& exchange);

    // Publishes to the default exchange with routing_key == queue name.
    // Returns false if the channel is not currently usable.
    bool Publish(const std::string& queue, const std::string& payload);

    // Publishes to a fanout exchange, which delivers to every bound queue.
    bool PublishToExchange(const std::string& exchange, const std::string& payload);

    // Starts consuming with manual acknowledgements and the given prefetch.
    void Consume(const std::string& queue, MessageHandler handler, uint16_t prefetch = 1);

    void StartTimer(uint32_t interval_seconds, bool fire_immediately);
    void StopTimer();

    void Stop(int exit_code = 0);

    bool connected() const { return channel_ready_; }
    const std::string& service_name() const { return service_name_; }

    // The one event loop of the process. Exposed so a subclass can attach its
    // own event sources to it — the web UI puts an evhttp listener here, which
    // is what keeps this project single-threaded even with an HTTP server in it.
    event_base* loop() const { return event_base_; }

  private:
    class Handler;
    friend class Handler;

    bool PublishTo(const std::string& exchange, const std::string& routing_key,
                   const std::string& payload);

    void Connect();
    void DoReconnect();
    void ScheduleReconnect();

    void HandleConnectionReady();
    void HandleConnectionFailure(const std::string& reason);
    void HandleDetached();

    std::string BuildAmqpAddress() const;

    static void OnTimerTrampoline(evutil_socket_t fd, short what, void* ctx);
    static void OnSignalTrampoline(evutil_socket_t fd, short what, void* ctx);
    static void OnReconnectTrampoline(evutil_socket_t fd, short what, void* ctx);
    static void OnForcedExitTrampoline(evutil_socket_t fd, short what, void* ctx);

    std::string service_name_;
    AmqpConfig config_;

    int exit_code_ = 0;
    uint32_t reconnect_attempts_ = 0;
    bool channel_ready_ = false;
    bool shutting_down_ = false;
    // A dropped connection can surface twice (connection error *and* channel
    // error); this keeps that from queueing two reconnects for one failure.
    bool reconnect_scheduled_ = false;

    // Re-armed after every reconnect, so it is remembered here.
    uint32_t timer_interval_seconds_ = 0;
    bool timer_armed_ = false;

    event_base* event_base_ = nullptr;
    std::unique_ptr<Handler> handler_;
    std::unique_ptr<AMQP::TcpConnection> connection_;
    std::unique_ptr<AMQP::TcpChannel> channel_;

    event* timer_event_ = nullptr;
    event* sigterm_event_ = nullptr;
    event* sigint_event_ = nullptr;
};

} // namespace habr::shared::messaging
