#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace aioairctrl {
using Json = nlohmann::json;
class TimeoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class CancelledError : public std::runtime_error {
public:
    CancelledError() : std::runtime_error("Client has been shut down") {}
};

struct ClientOptions {
    std::uint16_t port = 5683;
    // Zero lets the operating system choose the local UDP port. A fixed port
    // allows a narrow host-firewall rule for delayed Observe notifications.
    std::uint16_t local_port = 0;
    std::chrono::milliseconds timeout{10000};
    // Control requests can use a shorter deadline than the first Observe
    // response while both operations share one transport/session.
    std::chrono::milliseconds control_timeout{10000};
    // Zero uses timeout for the first Observe response.
    std::chrono::milliseconds observe_start_timeout{0};
    // Zero: wait indefinitely between notifications. The first response uses
    // observe_start_timeout when set, otherwise timeout.
    std::chrono::milliseconds observe_idle_timeout{0};
    // Empty CoAP CON messages keep stateful firewalls/NAT mappings alive while
    // the event-driven device has no changed status to report. Zero disables it.
    std::chrono::milliseconds observe_keepalive_interval{0};
    // Re-register Observe with the same token after a response timeout before
    // giving up and replacing the complete UDP/session state.
    unsigned observe_refresh_attempts = 0;
    // On a final Observe timeout, briefly receive the cancellation response so
    // it is not sent to an already closed UDP port. Zero disables the grace.
    std::chrono::milliseconds cancel_grace_timeout{0};
    std::function<void(const std::string&)> log;
};

class Client {
public:
    using StatusCallback = std::function<bool(const Json&)>;
    using StopPredicate = std::function<bool()>;

    // Opens UDP transport and synchronizes immediately; may throw.
    explicit Client(std::string host, ClientOptions options = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    Json get_status();
    // Callback returns false to finish. StopPredicate is polled at <=100 ms.
    void observe_status(StatusCallback callback, StopPredicate stop = {});
    bool set_control_value(const std::string& key, const Json& value,
                           int retry_count = 5, bool resync = true);
    bool set_control_values(const Json& data, int retry_count = 5, bool resync = true);
    void sync();
    // Safe during I/O. Cancels current operation; client cannot be reused.
    // Pending async operations own transport state until they finish.
    void shutdown() noexcept;

    std::future<Json> get_status_async();
    std::future<bool> set_control_values_async(Json data, int retry_count = 5,
                                              bool resync = true);
    std::future<void> observe_status_async(StatusCallback callback,
                                           StopPredicate stop = {});
    // One operation per client at a time; concurrent calls fail with logic_error.
    // Sequential Observe/control operations reuse the same UDP transport and key.
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
using CoAPClient = Client;
} // namespace aioairctrl
