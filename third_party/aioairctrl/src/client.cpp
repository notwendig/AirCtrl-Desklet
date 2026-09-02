#include <aioairctrl/client.hpp>
#include <aioairctrl/encryption.hpp>
#include "coap.hpp"
#include <atomic>
#include <mutex>
#include <utility>

namespace aioairctrl {
struct Client::Impl {
    std::atomic<bool> closed{false};
    ClientOptions options;
    detail::Transport transport;
    EncryptionContext encryption;
    std::mutex mutex;

    Impl(const std::string& host, ClientOptions opts)
        : options(std::move(opts)), transport(host, options.port, closed) {
        if (options.timeout.count() <= 0 || options.observe_idle_timeout.count() < 0)
            throw std::invalid_argument("Invalid timeout");
        sync_unlocked();
    }
    std::unique_lock<std::mutex> lock() {
        std::unique_lock<std::mutex> result(mutex, std::try_to_lock);
        if (!result.owns_lock()) throw std::logic_error("Client busy; use a separate Client for concurrent operations");
        if (closed.load()) throw CancelledError();
        return result;
    }
    void log(const std::string& text) { if (options.log) options.log(text); }
    void sync_unlocked() {
        if (closed.load()) throw CancelledError();
        log("Synchronizing /sys/dev/sync");
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string challenge;
        for (auto b : detail::random_bytes(4)) { challenge += hex[b >> 4]; challenge += hex[b & 15]; }
        const auto request = transport.request(2, "/sys/dev/sync", challenge);
        const auto response = transport.receive(request, options.timeout);
        encryption.set_client_key(response->payload);
    }
    void sync() { auto guard = lock(); sync_unlocked(); }
    Json status(const detail::Message& message) {
        const auto data = Json::parse(encryption.decrypt(message.payload));
        return data.at("state").at("reported");
    }
    Json get_status() {
        auto guard = lock();
        log("Reading /sys/dev/status");
        const auto request = transport.request(1, "/sys/dev/status", {}, 0);
        try {
            const auto response = transport.receive(request, options.timeout);
            auto result = status(*response);
            transport.cancel(request);
            return result;
        } catch (...) { transport.cancel(request); throw; }
    }
    void observe(StatusCallback callback, StopPredicate stop) {
        if (!callback) throw std::invalid_argument("Status callback is required");
        auto guard = lock();
        log("Observing /sys/dev/status");
        const auto request = transport.request(1, "/sys/dev/status", {}, 0);
        try {
            auto response = transport.receive(request, options.timeout, stop);
            if (response) {
                if (callback(status(*response))) {
                    if (!response->observe()) throw std::runtime_error("Device did not accept CoAP Observe");
                    for (;;) {
                        response = transport.receive(request, options.observe_idle_timeout, stop);
                        if (!response) break;
                        if (!callback(status(*response))) break;
                        if (!response->observe()) throw std::runtime_error("Device terminated CoAP Observe");
                    }
                }
            }
            transport.cancel(request);
        } catch (const CancelledError&) {
            transport.cancel(request); // shutdown is a normal end of observation
        } catch (...) { transport.cancel(request); throw; }
    }
    bool set(const Json& data, int retries, bool resync) {
        if (!data.is_object() || data.empty()) throw std::invalid_argument("Control data must be a nonempty JSON object");
        if (retries < 0) throw std::invalid_argument("Retry count must not be negative");
        auto guard = lock();
        Json desired = {{"CommandType", "app"}, {"DeviceId", ""}, {"EnduserId", ""}};
        desired.update(data);
        const auto payload = Json{{"state", {{"desired", desired}}}}.dump(-1, ' ', true);
        for (;;) {
            if (closed.load()) throw CancelledError();
            log("Setting /sys/dev/control");
            const auto request = transport.request(2, "/sys/dev/control", encryption.encrypt(payload));
            const auto response = transport.receive(request, options.timeout);
            const auto result = Json::parse(response->payload);
            if (!result.is_object()) throw std::runtime_error("Invalid control response: expected JSON object");
            if (result.contains("status") && result["status"] == "success") return true;
            // As upstream: resync even after final rejection, if requested.
            if (resync) sync_unlocked();
            if (retries == 0) return false;
            --retries;
            log("Control rejected, retrying");
        }
    }
};

Client::Client(std::string host, ClientOptions options)
    : impl_(std::make_shared<Impl>(host, std::move(options))) {}
Client::~Client() { shutdown(); }
Json Client::get_status() { return impl_->get_status(); }
void Client::observe_status(StatusCallback callback, StopPredicate stop) {
    impl_->observe(std::move(callback), std::move(stop));
}
bool Client::set_control_value(const std::string& key, const Json& value, int retries, bool resync) {
    return set_control_values(Json{{key, value}}, retries, resync);
}
bool Client::set_control_values(const Json& data, int retries, bool resync) { return impl_->set(data, retries, resync); }
void Client::sync() { impl_->sync(); }
void Client::shutdown() noexcept { impl_->closed.store(true); }
std::future<Json> Client::get_status_async() {
    return std::async(std::launch::async, [impl = impl_] { return impl->get_status(); });
}
std::future<bool> Client::set_control_values_async(Json data, int retries, bool resync) {
    return std::async(std::launch::async, [impl = impl_, data = std::move(data), retries, resync] {
        return impl->set(data, retries, resync);
    });
}
std::future<void> Client::observe_status_async(StatusCallback callback, StopPredicate stop) {
    return std::async(std::launch::async,
        [impl = impl_, callback = std::move(callback), stop = std::move(stop)]() mutable {
            impl->observe(std::move(callback), std::move(stop));
        });
}
} // namespace aioairctrl
