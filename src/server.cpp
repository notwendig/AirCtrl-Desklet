/**
 * @file server.cpp
 * @brief Qt-free TCP daemon and sole owner of the Philips UDP session.
 */
#include "airctrl_version.hpp"
#include <aioairctrl/client.hpp>

#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
constexpr std::size_t MaximumMessageBytes = 1024U * 1024U;
volatile sig_atomic_t signalStop = 0;

void signalHandler(int) { signalStop = 1; }

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

int strictInteger(const std::string& text, int minimum, int maximum) {
    std::size_t used = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &used, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("Ungültige Ganzzahl: " + text);
    }
    if (used != text.size() || value < minimum || value > maximum)
        throw std::runtime_error("Ganzzahl außerhalb des gültigen Bereichs: " + text);
    return static_cast<int>(value);
}

/** @brief Validated device endpoint and timeout policy from /etc/airctrld.cfg. */
struct DeviceConfig {
    std::string host = "AC2729-10";
    int port = 5683;
    int reconnectMs = 10000;
    int requestMs = 60000;
    int idleMs = 90000;
};

/** @brief Validated listener and device configuration used for one process run. */
struct ServerConfig {
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = 5680;
    DeviceConfig device;
};

/** @brief Parse the mandatory INI file without any Qt configuration classes. */
ServerConfig loadConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Konfiguration fehlt oder ist nicht lesbar: " + path);

    std::map<std::string, std::string> values;
    std::string section;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            if (section.empty())
                throw std::runtime_error("Leerer Abschnitt in " + path + ":" + std::to_string(lineNumber));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos || section.empty())
            throw std::runtime_error("Ungültige Zeile in " + path + ":" + std::to_string(lineNumber));
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty())
            throw std::runtime_error("Leerer Schlüssel oder Wert in " + path + ":" + std::to_string(lineNumber));
        values[section + "/" + key] = value;
    }
    if (!input.eof()) throw std::runtime_error("Fehler beim Lesen der Konfiguration: " + path);

    const auto value = [&values](const std::string& key, const std::string& fallback) {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    };
    const std::vector<std::string> allowed{
        "server/listen_address", "server/port", "device/host", "device/port",
        "device/reconnect_ms", "device/request_ms", "device/idle_ms"};
    for (const auto& entry : values) {
        if (std::find(allowed.begin(), allowed.end(), entry.first) == allowed.end())
            throw std::runtime_error("Unbekannte Einstellung: " + entry.first);
    }

    ServerConfig config;
    config.listenAddress = value("server/listen_address", "0.0.0.0");
    config.listenPort = static_cast<std::uint16_t>(
        strictInteger(value("server/port", "5680"), 1, 65535));
    config.device.host = value("device/host", "AC2729-10");
    config.device.port = strictInteger(value("device/port", "5683"), 1, 65535);
    config.device.reconnectMs = strictInteger(value("device/reconnect_ms", "10000"), 50, 300000);
    config.device.requestMs = strictInteger(value("device/request_ms", "60000"), 50, 86400000);
    config.device.idleMs = strictInteger(value("device/idle_ms", "90000"), 50, 86400000);
    if (config.listenAddress.empty() || config.device.host.empty())
        throw std::runtime_error("Server- oder Geräteadresse ist leer.");
    return config;
}

std::string controlValuesError(const Json& values) {
    if (!values.is_object() || values.empty()) return "Leerer Steuerauftrag.";
    const bool integers = values.begin().value().is_number_integer();
    for (const auto& item : values.items()) {
        const auto& key = item.key();
        const auto& value = item.value();
        const auto stringIs = [&value](std::initializer_list<const char*> allowed) {
            if (!value.is_string()) return false;
            const auto text = value.get<std::string>();
            return std::any_of(allowed.begin(), allowed.end(),
                               [&text](const char* candidate) { return text == candidate; });
        };
        const auto integerIs = [&value](std::initializer_list<int> allowed) {
            if (!value.is_number_integer()) return false;
            const auto number = value.get<long long>();
            return std::find(allowed.begin(), allowed.end(), number) != allowed.end();
        };
        const bool valid =
            (key == "pwr" && stringIs({"0", "1"})) ||
            (key == "cl" && value.is_boolean()) ||
            (key == "mode" && stringIs({"P", "A", "S", "M"})) ||
            (key == "om" && stringIs({"1", "2", "3", "s", "t"})) ||
            (key == "func" && stringIs({"P", "PH"})) ||
            (key == "uil" && stringIs({"0", "1"})) ||
            (key == "rhset" && integerIs({40, 50, 60, 70})) ||
            (key == "aqil" && integerIs({0, 25, 50, 75, 100})) ||
            (key == "dt" && value.is_number_integer() && value.get<long long>() >= 0 &&
             value.get<long long>() <= 12);
        if (!valid) return "Ungültiger Steuerwert: " + key;
        if (value.is_number_integer() != integers)
            return "Ein Steuerauftrag darf Ganzzahlen nicht mit Text- oder Boolean-Werten mischen.";
    }
    return {};
}

int createListener(const std::string& address, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* result = nullptr;
    const auto service = std::to_string(port);
    const int lookup = getaddrinfo(address.c_str(), service.c_str(), &hints, &result);
    if (lookup != 0)
        throw std::runtime_error("Ungültige Listenadresse: " + std::string(gai_strerror(lookup)));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(result, freeaddrinfo);
    int lastError = EADDRNOTAVAIL;
    for (auto* current = result; current; current = current->ai_next) {
        const int socketFd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socketFd < 0) { lastError = errno; continue; }
        int enabled = 1;
        setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (::bind(socketFd, current->ai_addr, current->ai_addrlen) == 0 &&
            ::listen(socketFd, 32) == 0)
            return socketFd;
        lastError = errno;
        ::close(socketFd);
    }
    throw std::runtime_error("TCP-Port kann nicht geöffnet werden: " + std::string(std::strerror(lastError)));
}

/** @brief One validated control request correlated to its originating TCP client. */
struct DeviceCommand {
    std::uint64_t client = 0;
    std::uint64_t id = 0;
    Json values;
};

struct ClientConnection {
    std::uint64_t id = 0;
    int socket = -1;
    std::atomic<bool> open{true};
    std::mutex writeMutex;
};

/**
 * @brief Bridges multiple TCP clients to one blocking Philips device session.
 *
 * The implementation uses only C++17 and POSIX sockets. Each TCP connection
 * has a reader thread; serialized writes are protected per connection. A
 * single device thread owns every UDP socket and all Philips protocol state.
 */
class AirCtrlServer final {
public:
    explicit AirCtrlServer(ServerConfig config)
        : config_(std::move(config)), exitOnIdle_(environmentFlag("AIRCTRL_SERVER_EXIT_ON_IDLE")) {}

    ~AirCtrlServer() { shutdown(); }

    void run() {
        listener_ = createListener(config_.listenAddress, config_.listenPort);
        startedAt_ = std::chrono::steady_clock::now();
        worker_ = std::thread([this] { deviceLoop(); });
        while (!stopping_.load() && !signalStop) {
            pollfd descriptor{listener_, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 100);
            if (ready < 0 && errno != EINTR)
                throw std::runtime_error("TCP-Warten fehlgeschlagen: " + std::string(std::strerror(errno)));
            if (ready > 0 && (descriptor.revents & POLLIN)) acceptClient();
            if (exitOnIdle_ && clientCount() == 0 &&
                (everHadClient_.load() ||
                 std::chrono::steady_clock::now() - startedAt_ >= std::chrono::seconds(5)))
                break;
        }
        shutdown();
    }

private:
    static bool environmentFlag(const char* name) {
        const char* value = std::getenv(name);
        return value && std::string(value) == "1";
    }

    void shutdown() {
        if (stopping_.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(deviceMutex_);
            reconnectRequested_ = true;
        }
        deviceWake_.notify_all();
        if (listener_ >= 0) {
            ::close(listener_);
            listener_ = -1;
        }
        std::vector<std::shared_ptr<ClientConnection>> clients;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (const auto& entry : clients_) clients.push_back(entry.second);
        }
        for (const auto& client : clients) closeClient(client);
        for (auto& thread : clientThreads_) if (thread.joinable()) thread.join();
        if (worker_.joinable()) worker_.join();
    }

    std::size_t clientCount() const {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        return clients_.size();
    }

    void acceptClient() {
        sockaddr_storage address{};
        socklen_t size = sizeof(address);
        const int socketFd = ::accept(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        if (socketFd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return;
            throw std::runtime_error("TCP-Client kann nicht angenommen werden: " +
                                     std::string(std::strerror(errno)));
        }
        auto client = std::make_shared<ClientConnection>();
        client->id = nextClient_.fetch_add(1);
        client->socket = socketFd;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_[client->id] = client;
        }
        everHadClient_.store(true);
        clientThreads_.emplace_back([this, client] { clientLoop(client); });
    }

    void closeClient(const std::shared_ptr<ClientConnection>& client) {
        if (!client || !client->open.exchange(false)) return;
        ::shutdown(client->socket, SHUT_RDWR);
        ::close(client->socket);
        std::lock_guard<std::mutex> lock(clientsMutex_);
        const auto found = clients_.find(client->id);
        if (found != clients_.end() && found->second == client) clients_.erase(found);
    }

    std::shared_ptr<ClientConnection> findClient(std::uint64_t id) const {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        const auto found = clients_.find(id);
        return found == clients_.end() ? nullptr : found->second;
    }

    void send(const std::shared_ptr<ClientConnection>& client, const Json& object) {
        if (!client || !client->open.load()) return;
        const std::string message = object.dump() + '\n';
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(client->writeMutex);
            std::size_t sent = 0;
            while (client->open.load() && sent < message.size()) {
                const auto count = ::send(client->socket, message.data() + sent,
                                          message.size() - sent, MSG_NOSIGNAL);
                if (count > 0) sent += static_cast<std::size_t>(count);
                else if (count < 0 && errno == EINTR) continue;
                else { failed = true; break; }
            }
        }
        if (failed) closeClient(client);
    }

    void send(std::uint64_t client, const Json& object) { send(findClient(client), object); }

    void broadcast(const Json& object) {
        std::vector<std::shared_ptr<ClientConnection>> clients;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (const auto& entry : clients_) clients.push_back(entry.second);
        }
        for (const auto& client : clients) send(client, object);
    }

    Json stateEnvelope() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        Json object{{"_airctrl", "state"}, {"state", state_}, {"starts", starts_}};
        if (!stateError_.empty()) object["error"] = stateError_;
        return object;
    }

    void clientLoop(const std::shared_ptr<ClientConnection>& client) {
        send(client, stateEnvelope());
        Json cached;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (state_ == "connected") cached = lastStatus_;
        }
        if (!cached.empty()) send(client, {{"_airctrl", "status"}, {"data", cached}});

        std::string buffer;
        char incoming[8192];
        while (!stopping_.load() && client->open.load()) {
            const auto count = ::recv(client->socket, incoming, sizeof(incoming), 0);
            if (count == 0) break;
            if (count < 0) {
                if (errno == EINTR) continue;
                break;
            }
            buffer.append(incoming, static_cast<std::size_t>(count));
            if (buffer.size() > MaximumMessageBytes) {
                send(client, {{"_airctrl", "error"}, {"error", "IPC-Nachricht ist zu groß."}});
                break;
            }
            for (;;) {
                const auto newline = buffer.find('\n');
                if (newline == std::string::npos) break;
                auto line = trim(buffer.substr(0, newline));
                buffer.erase(0, newline + 1);
                if (line.empty()) continue;
                try {
                    const auto request = Json::parse(line);
                    if (!request.is_object()) throw std::runtime_error("not an object");
                    handle(client->id, request);
                } catch (const std::exception&) {
                    send(client, {{"_airctrl", "error"}, {"error", "Ungültiges IPC-JSON."}});
                }
            }
        }
        closeClient(client);
    }

    void handle(std::uint64_t client, const Json& request) {
        const auto kind = request.value("_airctrl", std::string{});
        if (kind == "configure") {
            send(client, {{"_airctrl", "error"},
                          {"error", "Geräteeinstellungen gehören ausschließlich in /etc/airctrld.cfg."}});
            return;
        }
        if (kind == "refresh") {
            deviceReady_.store(false);
            std::deque<DeviceCommand> pending;
            {
                std::lock_guard<std::mutex> lock(deviceMutex_);
                if (reconnectRequested_) return;
                reconnectRequested_ = true;
                pending.swap(commands_);
            }
            for (const auto& command : pending)
                postControl(command, false, "Geräte-I/O wird erneuert; Befehl nicht ausgeführt.");
            deviceWake_.notify_all();
            return;
        }
        if (kind == "control") {
            std::uint64_t id = 0;
            Json values = Json::object();
            try {
                if (request.contains("id") && request["id"].is_number_unsigned())
                    id = request["id"].get<std::uint64_t>();
                else if (request.contains("id") && request["id"].is_number_integer()) {
                    const auto signedId = request["id"].get<long long>();
                    if (signedId > 0) id = static_cast<std::uint64_t>(signedId);
                }
                if (request.contains("values") && request["values"].is_object())
                    values = request["values"];
            } catch (const std::exception&) {
                id = 0;
            }
            const auto problem = controlValuesError(values);
            if (id == 0 || !problem.empty()) {
                send(client, {{"_airctrl", "control"}, {"id", id}, {"ok", false},
                              {"error", problem.empty() ? "Ungültige Befehlskennung." : problem}});
                return;
            }
            if (!deviceReady_.load()) {
                send(client, {{"_airctrl", "control"}, {"id", id}, {"ok", false},
                              {"error", "Der Server hat noch keine aktive Geräteverbindung."}});
                return;
            }
            {
                std::lock_guard<std::mutex> lock(deviceMutex_);
                commands_.push_back({client, id, std::move(values)});
            }
            deviceWake_.notify_all();
            return;
        }
        if (kind == "ping") {
            send(client, {{"_airctrl", "pong"}});
            return;
        }
        send(client, {{"_airctrl", "error"}, {"error", "Unbekannter IPC-Befehl."}});
    }

    void postState(std::string state, std::string error = {}) {
        Json envelope;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = std::move(state);
            stateError_ = std::move(error);
            envelope = {{"_airctrl", "state"}, {"state", state_}, {"starts", starts_}};
            if (!stateError_.empty()) envelope["error"] = stateError_;
        }
        broadcast(envelope);
    }

    void postStart() {
        Json envelope;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            ++starts_;
            state_ = "connecting";
            stateError_.clear();
            envelope = {{"_airctrl", "state"}, {"state", state_}, {"starts", starts_}};
        }
        broadcast(envelope);
    }

    void postStatus(const Json& status) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastStatus_ = status;
            state_ = "connected";
            stateError_.clear();
        }
        broadcast({{"_airctrl", "status"}, {"data", status}});
    }

    void postControl(const DeviceCommand& command, bool ok, std::string error = {}) {
        Json result{{"_airctrl", "control"}, {"id", command.id}, {"ok", ok}};
        if (!error.empty()) result["error"] = std::move(error);
        send(command.client, result);
    }

    bool interrupted(std::uint64_t generation, bool commandsInterrupt = true) const {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        return stopping_.load() || reconnectRequested_ || configGeneration_ != generation ||
               (commandsInterrupt && !commands_.empty());
    }

    void waitReconnect(int milliseconds, std::uint64_t generation) {
        std::unique_lock<std::mutex> lock(deviceMutex_);
        deviceWake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this, generation] {
            return stopping_.load() || reconnectRequested_ || configGeneration_ != generation ||
                   !commands_.empty();
        });
    }

    bool takeCommand(DeviceCommand* command) {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (commands_.empty()) return false;
        *command = std::move(commands_.front());
        commands_.pop_front();
        return true;
    }

    /** @brief Own every Philips socket and rebuild it only outside an iteration. */
    void deviceLoop() {
        while (!stopping_.load()) {
            DeviceConfig config;
            std::uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(deviceMutex_);
                config = config_.device;
                generation = configGeneration_;
                reconnectRequested_ = false;
            }
            deviceReady_.store(false);
            postStart();
            try {
                aioairctrl::ClientOptions options;
                options.port = static_cast<std::uint16_t>(config.port);
                options.timeout = std::chrono::milliseconds(config.requestMs);
                options.control_timeout = std::chrono::seconds(10);
                options.observe_idle_timeout = std::chrono::milliseconds(config.idleMs);
                aioairctrl::Client device(config.host, options);
                bool statusRequiredAfterControl = false;
                while (!stopping_.load()) {
                    if (interrupted(generation, !statusRequiredAfterControl)) {
                        std::lock_guard<std::mutex> lock(deviceMutex_);
                        if (reconnectRequested_ || configGeneration_ != generation) break;
                    }
                    device.observe_status(
                        [this, &statusRequiredAfterControl](const Json& status) {
                            statusRequiredAfterControl = false;
                            deviceReady_.store(true);
                            postStatus(status);
                            return true;
                        },
                        [this, generation, &statusRequiredAfterControl] {
                            return interrupted(generation, !statusRequiredAfterControl);
                        });

                    {
                        std::lock_guard<std::mutex> lock(deviceMutex_);
                        if (reconnectRequested_ || configGeneration_ != generation || stopping_.load()) break;
                    }
                    DeviceCommand command;
                    if (takeCommand(&command)) {
                        // Require a status after every attempt before another
                        // client's command may leave the shared UDP session.
                        statusRequiredAfterControl = true;
                        try {
                            const bool accepted = device.set_control_values(command.values, 0, false);
                            bool configurationChanged = false;
                            {
                                std::lock_guard<std::mutex> lock(deviceMutex_);
                                configurationChanged = reconnectRequested_ || configGeneration_ != generation;
                            }
                            if (configurationChanged)
                                postControl(command, false,
                                    "Geräte-I/O wurde während des Schaltbefehls erneuert · Ausgang unbekannt; keine Wiederholung.");
                            else
                                postControl(command, accepted,
                                    accepted ? std::string{} : "Gerät hat den Schaltbefehl abgelehnt.");
                        } catch (const std::exception& error) {
                            postControl(command, false, error.what());
                        }
                    }
                }
            } catch (const std::exception& error) {
                deviceReady_.store(false);
                postState("error", error.what());
                DeviceCommand command;
                while (takeCommand(&command))
                    postControl(command, false,
                                "Geräte-I/O wurde unterbrochen; Befehl nicht wiederholt.");
                if (!stopping_.load()) waitReconnect(config.reconnectMs, generation);
            }
            deviceReady_.store(false);
        }
    }

    ServerConfig config_;
    const bool exitOnIdle_;
    int listener_ = -1;
    std::chrono::steady_clock::time_point startedAt_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> everHadClient_{false};
    std::atomic<bool> deviceReady_{false};
    std::atomic<std::uint64_t> nextClient_{1};

    mutable std::mutex clientsMutex_;
    std::map<std::uint64_t, std::shared_ptr<ClientConnection>> clients_;
    std::vector<std::thread> clientThreads_;

    mutable std::mutex stateMutex_;
    std::string state_ = "starting";
    std::string stateError_;
    std::uint64_t starts_ = 0;
    Json lastStatus_;

    mutable std::mutex deviceMutex_;
    std::condition_variable deviceWake_;
    std::uint64_t configGeneration_ = 1;
    bool reconnectRequested_ = false;
    std::deque<DeviceCommand> commands_;
    std::thread worker_;
};

void usage(std::ostream& output) {
    output << "AirControl-TCP-Server; einziger Prozess mit AC2729-Zugriff\n"
           << "Aufruf: airctrl-server [--config DATEI] [--check-config] [--version] [--help]\n";
}
} // namespace

int main(int argc, char** argv) {
    std::string configPath = "/etc/airctrld.cfg";
    bool checkConfig = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" && index + 1 < argc) configPath = argv[++index];
        else if (argument.rfind("--config=", 0) == 0) configPath = argument.substr(9);
        else if (argument == "--check-config") checkConfig = true;
        else if (argument == "--version" || argument == "-v") {
            std::cout << "airctrl-server " << AIRCTRL_VERSION << '\n';
            return 0;
        } else if (argument == "--help" || argument == "-h") {
            usage(std::cout);
            return 0;
        } else {
            std::cerr << "Unbekannte Option: " << argument << '\n';
            usage(std::cerr);
            return 2;
        }
    }
    if (const char* testConfig = std::getenv("AIRCTRL_TEST_SERVER_CONFIG")) {
        if (*testConfig) configPath = testConfig;
    }
    try {
        auto config = loadConfig(configPath);
        if (checkConfig) {
            std::cout << "Konfiguration gültig: " << configPath << '\n';
            return 0;
        }
        struct sigaction action{};
        action.sa_handler = signalHandler;
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        signal(SIGPIPE, SIG_IGN);
        AirCtrlServer server(std::move(config));
        server.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AirControl-Server: " << error.what() << '\n';
        return checkConfig ? 2 : 1;
    }
}
