#pragma once
#include <aioairctrl/client.hpp>
#include <atomic>
#include <optional>
#include <vector>

namespace aioairctrl::detail {
using Bytes = std::vector<unsigned char>;
struct Option { unsigned number; Bytes value; };
struct Message {
    unsigned type = 1; // NON
    unsigned code = 0;
    std::uint16_t mid = 0;
    Bytes token;
    std::vector<Option> options;
    std::string payload;
    std::optional<std::uint32_t> observe() const;
};
Bytes encode(const Message& message);
Message decode(const Bytes& wire);
Bytes random_bytes(std::size_t count);

class Transport {
public:
    Transport(const std::string& host, std::uint16_t port,
              const std::atomic<bool>& closed);
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Message request(unsigned code, const std::string& path,
                    std::string payload = {}, std::optional<unsigned> observe = {},
                    Bytes token = {});
    void send(const Message& message);
    // Returns nullopt for StopPredicate, throws on timeout/shutdown/protocol errors.
    std::optional<Message> receive(const Message& request,
        std::chrono::milliseconds timeout, const Client::StopPredicate& stop = {});
    void cancel(const Message& original) noexcept;
private:
    int fd_ = -1;
    std::uint16_t mid_ = 0;
    std::string host_;
    std::uint16_t port_;
    const std::atomic<bool>& closed_;
};
} // namespace aioairctrl::detail
