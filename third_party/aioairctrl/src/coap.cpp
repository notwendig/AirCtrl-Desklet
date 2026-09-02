#include "coap.hpp"
#include <openssl/rand.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>

namespace aioairctrl::detail {
namespace {
[[noreturn]] void io_error(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
unsigned extension(unsigned value, Bytes& out) {
    if (value < 13) return value;
    if (value < 269) { out.push_back(static_cast<unsigned char>(value - 13)); return 13; }
    if (value > 65804) throw std::length_error("CoAP option too large");
    value -= 269;
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value));
    return 14;
}
unsigned read_extension(unsigned nibble, const Bytes& wire, std::size_t& pos) {
    if (nibble < 13) return nibble;
    if (nibble == 15) throw std::runtime_error("Reserved CoAP option nibble");
    const std::size_t size = nibble == 13 ? 1 : 2;
    if (wire.size() - pos < size) throw std::runtime_error("Truncated CoAP option extension");
    unsigned value = wire[pos++];
    if (size == 2) value = (value << 8) | wire[pos++];
    return value + (size == 1 ? 13U : 269U);
}
Bytes integer(unsigned value) {
    Bytes out;
    while (value) { out.insert(out.begin(), static_cast<unsigned char>(value)); value >>= 8; }
    return out;
}
void check_response(const Message& message) {
    if (message.code / 32 != 2)
        throw std::runtime_error("CoAP error " + std::to_string(message.code / 32) + "." +
            std::to_string(message.code % 32) + ": " + message.payload);
    for (const auto& option : message.options) {
        if (option.number == 23 || option.number == 27)
            throw std::runtime_error("Blockwise CoAP responses are not supported by this device-specific transport");
        if ((option.number & 1U) && option.number != 1 && option.number != 3 &&
            option.number != 5 && option.number != 7 && option.number != 11 &&
            option.number != 15 && option.number != 17 && option.number != 35 && option.number != 39)
            throw std::runtime_error("Unknown critical CoAP option " + std::to_string(option.number));
    }
}
} // namespace

Bytes random_bytes(std::size_t count) {
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Random request too large");
    Bytes result(count);
    if (RAND_bytes(result.data(), static_cast<int>(count)) != 1)
        throw std::runtime_error("OpenSSL random generator failed");
    return result;
}
std::optional<std::uint32_t> Message::observe() const {
    std::optional<std::uint32_t> result;
    for (const auto& option : options) {
        if (option.number != 6) continue;
        if (result || option.value.size() > 3) throw std::runtime_error("Invalid Observe option");
        std::uint32_t value = 0;
        for (auto byte : option.value) value = (value << 8) | byte;
        result = value;
    }
    return result;
}
Bytes encode(const Message& message) {
    if (message.type > 3 || message.code > 255 || message.token.size() > 8)
        throw std::invalid_argument("Invalid CoAP header");
    Bytes result{static_cast<unsigned char>(0x40U | (message.type << 4) | message.token.size()),
        static_cast<unsigned char>(message.code), static_cast<unsigned char>(message.mid >> 8),
        static_cast<unsigned char>(message.mid)};
    result.insert(result.end(), message.token.begin(), message.token.end());
    unsigned previous = 0;
    for (const auto& option : message.options) {
        if (option.number < previous || option.value.size() > 65804)
            throw std::invalid_argument("Invalid CoAP option ordering/length");
        Bytes delta_bytes, length_bytes;
        const auto delta = extension(option.number - previous, delta_bytes);
        const auto length = extension(static_cast<unsigned>(option.value.size()), length_bytes);
        result.push_back(static_cast<unsigned char>((delta << 4) | length));
        result.insert(result.end(), delta_bytes.begin(), delta_bytes.end());
        result.insert(result.end(), length_bytes.begin(), length_bytes.end());
        result.insert(result.end(), option.value.begin(), option.value.end());
        previous = option.number;
    }
    if (!message.payload.empty()) {
        result.push_back(0xff);
        result.insert(result.end(), message.payload.begin(), message.payload.end());
    }
    return result;
}
Message decode(const Bytes& wire) {
    if (wire.size() < 4 || wire[0] >> 6 != 1 || (wire[0] & 15) > 8)
        throw std::runtime_error("Invalid CoAP header");
    Message result;
    result.type = (wire[0] >> 4) & 3;
    result.code = wire[1];
    result.mid = static_cast<std::uint16_t>((wire[2] << 8) | wire[3]);
    std::size_t pos = 4 + (wire[0] & 15U);
    if (pos > wire.size()) throw std::runtime_error("Truncated CoAP token");
    result.token.assign(wire.begin() + 4, wire.begin() + static_cast<std::ptrdiff_t>(pos));
    if (result.code == 0 && wire.size() != 4) throw std::runtime_error("Invalid empty CoAP message");
    unsigned number = 0;
    while (pos < wire.size()) {
        const unsigned byte = wire[pos++];
        if (byte == 255) {
            if (pos == wire.size()) throw std::runtime_error("Empty CoAP payload after marker");
            result.payload.assign(wire.begin() + static_cast<std::ptrdiff_t>(pos), wire.end());
            break;
        }
        const auto delta = read_extension(byte >> 4, wire, pos);
        const auto length = read_extension(byte & 15, wire, pos);
        if (delta > 65535 || number > 65535 - delta || length > wire.size() - pos)
            throw std::runtime_error("Invalid CoAP option bounds");
        number += delta;
        result.options.push_back({number, Bytes(wire.begin() + static_cast<std::ptrdiff_t>(pos),
            wire.begin() + static_cast<std::ptrdiff_t>(pos + length))});
        pos += length;
    }
    return result;
}

Transport::Transport(const std::string& host, std::uint16_t port, const std::atomic<bool>& closed)
    : host_(host), port_(port), closed_(closed) {
    if (host_.size() > 2 && host_.front() == '[' && host_.back() == ']')
        host_ = host_.substr(1, host_.size() - 2);
    if (host_.empty() || host_.find('/') != std::string::npos || !port_)
        throw std::invalid_argument("Expected a hostname or IP address and a nonzero port");
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* raw = nullptr;
    const auto service = std::to_string(port_);
    const int status = getaddrinfo(host_.c_str(), service.c_str(), &hints, &raw);
    if (status) throw std::runtime_error(std::string("Resolve host: ") + gai_strerror(status));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
    const auto random = random_bytes(2);
    mid_ = static_cast<std::uint16_t>((random[0] << 8) | random[1]);
    for (auto* address = raw; address; address = address->ai_next) {
        fd_ = socket(address->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, address->ai_protocol);
        if (fd_ < 0) continue;
        if (::connect(fd_, address->ai_addr, address->ai_addrlen) == 0) return;
        ::close(fd_);
        fd_ = -1;
    }
    io_error("Open UDP socket");
}
Transport::~Transport() { if (fd_ >= 0) ::close(fd_); }

Message Transport::request(unsigned code, const std::string& path, std::string payload,
                            std::optional<unsigned> observe, Bytes token) {
    Message message;
    message.code = code;
    message.mid = ++mid_;
    message.token = token.empty() ? random_bytes(8) : std::move(token);
    // Uri-Host, Observe, Uri-Port, Uri-Path, in option-number order.
    message.options.push_back({3, Bytes(host_.begin(), host_.end())});
    if (observe) message.options.push_back({6, integer(*observe)});
    if (port_ != 5683) message.options.push_back({7, integer(port_)});
    std::size_t start = 0;
    while (start < path.size()) {
        if (path[start] == '/') { ++start; continue; }
        const auto end = path.find('/', start);
        const auto segment = path.substr(start, end == std::string::npos ? end : end - start);
        message.options.push_back({11, Bytes(segment.begin(), segment.end())});
        if (end == std::string::npos) break;
        start = end + 1;
    }
    message.payload = std::move(payload);
    send(message);
    return message;
}
void Transport::send(const Message& message) {
    const auto bytes = encode(message);
    if (bytes.size() > 65507) throw std::length_error("CoAP message exceeds UDP datagram limit");
    ssize_t sent;
    do { sent = ::send(fd_, bytes.data(), bytes.size(), 0); } while (sent < 0 && errno == EINTR);
    if (sent < 0) io_error("Send UDP");
    if (static_cast<std::size_t>(sent) != bytes.size()) throw std::runtime_error("Short UDP write");
}
std::optional<Message> Transport::receive(const Message& request_message,
    std::chrono::milliseconds timeout, const Client::StopPredicate& stop) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (closed_.load()) throw CancelledError();
        if (stop && stop()) return std::nullopt;
        int wait_ms = 100;
        if (timeout.count() > 0) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) throw TimeoutError("CoAP response timed out");
            wait_ms = static_cast<int>(std::min<std::int64_t>(100, remaining));
        }
        pollfd descriptor{fd_, POLLIN, 0};
        const int ready = poll(&descriptor, 1, wait_ms);
        if (ready < 0) { if (errno == EINTR) continue; io_error("Poll UDP"); }
        if (ready == 0) continue;
        Bytes buffer(65536);
        const auto size = recv(fd_, buffer.data(), buffer.size(), 0);
        if (size < 0) { if (errno == EINTR) continue; io_error("Receive UDP"); }
        buffer.resize(static_cast<std::size_t>(size));
        auto response = decode(buffer);
        if (response.type == 3) {
            if (response.mid == request_message.mid) throw std::runtime_error("CoAP request reset by peer");
            continue;
        }
        if (response.code == 0) continue;
        if (response.token != request_message.token) {
            if (response.type == 0) send(Message{3, 0, response.mid, {}, {}, {}});
            continue;
        }
        if (response.type == 0) send(Message{2, 0, response.mid, {}, {}, {}});
        // Philips devices can reuse MIDs: deliberately no MID deduplication,
        // mirroring the aiocoap monkeypatch in the upstream implementation.
        check_response(response);
        return response;
    }
}
void Transport::cancel(const Message& original) noexcept {
    try { (void)request(1, "/sys/dev/status", {}, 1, original.token); } catch (...) {}
}
} // namespace aioairctrl::detail
