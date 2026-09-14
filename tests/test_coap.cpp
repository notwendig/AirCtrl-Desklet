/**
 * @file test_coap.cpp
 * @brief Loopback regressions for the fixed-port CoAP transport.
 */
#include <aioairctrl/client.hpp>
#include "coap.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

using aioairctrl::detail::Bytes;
using aioairctrl::detail::Message;

std::uint16_t bindLoopback(int* descriptor) {
    *descriptor = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (*descriptor < 0) throw std::runtime_error("socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(*descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        throw std::runtime_error("bind failed");
    socklen_t size = sizeof(address);
    if (::getsockname(*descriptor, reinterpret_cast<sockaddr*>(&address), &size) != 0)
        throw std::runtime_error("getsockname failed");
    return ntohs(address.sin_port);
}

Message receiveMessage(int descriptor, sockaddr_in* peer) {
    pollfd ready{descriptor, POLLIN, 0};
    if (::poll(&ready, 1, 2000) <= 0) throw std::runtime_error("test peer timed out");
    Bytes wire(65536);
    socklen_t size = sizeof(*peer);
    const ssize_t count = ::recvfrom(descriptor, wire.data(), wire.size(), 0,
                                     reinterpret_cast<sockaddr*>(peer), &size);
    if (count < 0) throw std::runtime_error("test receive failed");
    wire.resize(static_cast<std::size_t>(count));
    return aioairctrl::detail::decode(wire);
}

void sendMessage(int descriptor, const sockaddr_in& peer, const Message& message) {
    const Bytes wire = aioairctrl::detail::encode(message);
    if (::sendto(descriptor, wire.data(), wire.size(), 0,
                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) < 0)
        throw std::runtime_error("test send failed");
}

} // namespace

int main() {
    int server = -1;
    const std::uint16_t serverPort = bindLoopback(&server);
    int portProbe = -1;
    const std::uint16_t clientPort = bindLoopback(&portProbe);
    ::close(portProbe);

    std::atomic<bool> closed{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> cancellationReplySent{false};
    std::atomic<int> pings{0};
    std::atomic<int> observedClientPort{0};
    std::thread peer([&] {
        try {
            sockaddr_in client{};
            const Message observation = receiveMessage(server, &client);
            observedClientPort.store(ntohs(client.sin_port));
            for (;;) {
                const Message message = receiveMessage(server, &client);
                observedClientPort.store(ntohs(client.sin_port));
                if (message.code != 0 || ++pings < 2) continue;
                Message status{1, 69, 400, observation.token, {{6, {1}}}, "status"};
                sendMessage(server, client, status);
                break;
            }

            const Message cancellation = receiveMessage(server, &client);
            if (cancellation.observe() != 1 || cancellation.token != observation.token)
                throw std::runtime_error("invalid Observe cancellation");
            Message lateStatus{1, 69, 401, observation.token, {{6, {2}}}, "late-status"};
            sendMessage(server, client, lateStatus);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            Message cancellationReply{1, 69, cancellation.mid, cancellation.token, {}, "cancelled"};
            cancellationReplySent.store(true);
            sendMessage(server, client, cancellationReply);
        } catch (...) {
            failed.store(true);
        }
    });

    int result = 0;
    try {
        aioairctrl::detail::Transport transport("127.0.0.1", serverPort, clientPort, closed);
        const Message request = transport.request(1, "/sys/dev/status", {}, 0);
        const std::optional<Message> response = transport.receive(
            request, std::chrono::milliseconds(1000), {}, std::chrono::milliseconds(40));
        if (!response || response->payload != "status" || pings.load() < 2 ||
            observedClientPort.load() != clientPort)
            throw std::runtime_error("fixed port or keepalive regression");
        transport.cancel(request, std::chrono::milliseconds(300));
        if (!cancellationReplySent.load())
            throw std::runtime_error("cancellation grace closed on a late notification");
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        result = 1;
    }

    peer.join();
    ::close(server);
    if (failed.load()) {
        std::cerr << "loopback peer failed\n";
        result = 1;
    }
    return result;
}
