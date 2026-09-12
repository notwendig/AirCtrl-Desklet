/**
 * @file server.cpp
 * @brief Qt-independent multi-client TCP server and Philips UDP-session owner.
 */
#include "server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace airctrl {
namespace {
constexpr std::size_t maximumInputSize = 1024U * 1024U;
constexpr std::size_t maximumOutputSize = 4U * 1024U * 1024U;
volatile sig_atomic_t terminationRequested = 0;

void requestTermination(int) { terminationRequested = 1; }

std::string trim(std::string text) {
    const std::string whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1U);
}

bool parseInteger(const std::string& text, int minimum, int maximum, int* value) {
    int parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < minimum || parsed > maximum)
        return false;
    *value = parsed;
    return true;
}

/** @brief Parse the mandatory INI configuration without a Qt dependency. */
bool loadConfig(const std::string& path, ServerConfig* config, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "Konfiguration fehlt oder kann nicht gelesen werden: " + path;
        return false;
    }
    std::unordered_map<std::string, std::string> values;
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.size() >= 2U && line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1U, line.size() - 2U));
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || section.empty()) {
            if (error) *error = "Ungültige Zeile in " + path;
            return false;
        }
        values[section + "/" + trim(line.substr(0U, separator))] = trim(line.substr(separator + 1U));
    }
    if (!file.eof()) {
        if (error) *error = "Konfiguration kann nicht gelesen werden: " + path;
        return false;
    }
    const std::unordered_map<std::string, std::string>::const_iterator address =
        values.find("server/listen_address");
    const std::unordered_map<std::string, std::string>::const_iterator host = values.find("device/host");
    config->listenAddress = address == values.end() ? "0.0.0.0" : trim(address->second);
    config->device.host = host == values.end() ? "AC2729-10" : trim(host->second);
    int listenPort = 5680;
    int devicePort = 5683;
    int reconnectMs = 10000;
    int requestMs = 60000;
    int idleMs = 90000;
    const struct Setting {
        const char* name;
        int minimum;
        int maximum;
        int* destination;
    } settings[] = {
        {"server/port", 1, 65535, &listenPort},
        {"device/port", 1, 65535, &devicePort},
        {"device/reconnect_ms", 50, 300000, &reconnectMs},
        {"device/request_ms", 50, 86400000, &requestMs},
        {"device/idle_ms", 50, 86400000, &idleMs},
    };
    for (const Setting& setting : settings) {
        const std::unordered_map<std::string, std::string>::const_iterator found = values.find(setting.name);
        if (found != values.end() &&
            !parseInteger(found->second, setting.minimum, setting.maximum, setting.destination)) {
            if (error) *error = "Ungültiger Wert in " + path + ": " + setting.name;
            return false;
        }
    }
    in_addr ipv4{};
    in6_addr ipv6{};
    if (config->listenAddress.empty() ||
        (inet_pton(AF_INET, config->listenAddress.c_str(), &ipv4) != 1 &&
         inet_pton(AF_INET6, config->listenAddress.c_str(), &ipv6) != 1) ||
        config->device.host.empty()) {
        if (error) *error = "Ungültiger Wert in " + path;
        return false;
    }
    config->listenPort = static_cast<std::uint16_t>(listenPort);
    config->device.port = devicePort;
    config->device.reconnectMs = reconnectMs;
    config->device.requestMs = requestMs;
    config->device.idleMs = idleMs;
    return true;
}

std::string controlValuesError(const Json& values) {
    if (!values.is_object() || values.empty()) return "Leerer Steuerauftrag.";
    const bool numbers = values.begin().value().is_number();
    for (Json::const_iterator item = values.begin(); item != values.end(); ++item) {
        const Json& value = item.value();
        const std::string& key = item.key();
        const bool isNumber = value.is_number();
        double number = -1.0;
        if (isNumber) number = value.get<double>();
        const bool valid =
            (key == "pwr" && value.is_string() && (value == "0" || value == "1")) ||
            (key == "cl" && value.is_boolean()) ||
            (key == "mode" && value.is_string() &&
             (value == "P" || value == "A" || value == "S" || value == "M")) ||
            (key == "om" && value.is_string() &&
             (value == "1" || value == "2" || value == "3" || value == "s" || value == "t")) ||
            (key == "func" && value.is_string() && (value == "P" || value == "PH")) ||
            (key == "uil" && value.is_string() && (value == "0" || value == "1")) ||
            (key == "rhset" && isNumber &&
             (number == 40.0 || number == 50.0 || number == 60.0 || number == 70.0)) ||
            (key == "aqil" && isNumber &&
             (number == 0.0 || number == 25.0 || number == 50.0 || number == 75.0 || number == 100.0)) ||
            (key == "dt" && isNumber && number >= 0.0 && number <= 12.0 &&
             number == static_cast<double>(static_cast<int>(number)));
        if (!valid) return "Ungültiger Steuerwert: " + key;
        if (isNumber != numbers)
            return "Ein Steuerauftrag darf Ganzzahlen nicht mit Text- oder Boolean-Werten mischen.";
    }
    return {};
}

bool requestId(const Json& request, std::uint64_t* id) {
    const Json::const_iterator found = request.find("id");
    if (found == request.end()) return false;
    try {
        if (found->is_number_unsigned()) *id = found->get<std::uint64_t>();
        else if (found->is_number_integer()) {
            const std::int64_t signedId = found->get<std::int64_t>();
            if (signedId <= 0) return false;
            *id = static_cast<std::uint64_t>(signedId);
        } else return false;
    } catch (const Json::exception&) {
        return false;
    }
    return *id != 0U;
}

int setNonBlocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
}

} // namespace

AirCtrlServer::AirCtrlServer(ServerConfig config)
    : config_(std::move(config)), exitOnIdle_(environmentFlag("AIRCTRL_SERVER_EXIT_ON_IDLE")) {}

AirCtrlServer::~AirCtrlServer() { shutdown(); }

bool AirCtrlServer::start(std::string* error) {
    if (!openWakePipe(error) || !openListener(error)) return false;
    worker_ = std::thread([this] { deviceLoop(); });
    initialIdleDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return true;
}

int AirCtrlServer::run() {
    while (!quitRequested_ && !terminationRequested) {
        std::vector<pollfd> descriptors;
        std::vector<std::uint64_t> clientIds;
        descriptors.push_back({listener_, POLLIN, 0});
        descriptors.push_back({wakeRead_, POLLIN, 0});
        for (const std::pair<const std::uint64_t, ClientConnection>& entry : clients_) {
            short events = POLLIN;
            if (!entry.second.output.empty()) events = static_cast<short>(events | POLLOUT);
            descriptors.push_back({entry.second.descriptor, events, 0});
            clientIds.push_back(entry.first);
        }
        const int result = poll(descriptors.data(), descriptors.size(), 100);
        if (result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "AirControl-Server: poll fehlgeschlagen: " << std::strerror(errno) << '\n';
            return 1;
        }
        if ((descriptors[0].revents & POLLIN) != 0) acceptClients();
        if ((descriptors[1].revents & POLLIN) != 0) processEvents();
        for (std::size_t index = 0; index < clientIds.size(); ++index) {
            const short events = descriptors[index + 2U].revents;
            if (events == 0) continue;
            const std::uint64_t id = clientIds[index];
            if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                closeClient(id);
                continue;
            }
            if ((events & POLLIN) != 0 && clients_.find(id) != clients_.end()) readClient(id);
            if ((events & POLLOUT) != 0 && clients_.find(id) != clients_.end()) flushClient(id);
        }
        refreshScheduled_ = false;
        if (exitOnIdle_ && clients_.empty() && !acceptedAnyClient_ &&
            std::chrono::steady_clock::now() >= initialIdleDeadline_)
            quitRequested_ = true;
    }
    return 0;
}

bool AirCtrlServer::environmentFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

bool AirCtrlServer::openWakePipe(std::string* error) {
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0 || setNonBlocking(descriptors[0]) != 0 ||
        setNonBlocking(descriptors[1]) != 0) {
        if (descriptors[0] >= 0) close(descriptors[0]);
        if (descriptors[1] >= 0) close(descriptors[1]);
        if (error) *error = std::string("Interne Signalleitung fehlgeschlagen: ") + std::strerror(errno);
        return false;
    }
    wakeRead_ = descriptors[0];
    wakeWrite_ = descriptors[1];
    return true;
}

bool AirCtrlServer::openListener(std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(config_.listenPort);
    const int lookup = getaddrinfo(config_.listenAddress.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        if (error) *error = std::string("Ungültige Listenadresse: ") + gai_strerror(lookup);
        return false;
    }
    std::string lastError = "Kein passender Socket.";
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int descriptor = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (descriptor < 0) {
            lastError = std::strerror(errno);
            continue;
        }
        const int enabled = 1;
        setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (setNonBlocking(descriptor) == 0 &&
            bind(descriptor, address->ai_addr, address->ai_addrlen) == 0 &&
            listen(descriptor, SOMAXCONN) == 0) {
            listener_ = descriptor;
            break;
        }
        lastError = std::strerror(errno);
        close(descriptor);
    }
    freeaddrinfo(addresses);
    if (listener_ < 0) {
        if (error) *error = lastError;
        return false;
    }
    return true;
}

void AirCtrlServer::shutdown() {
    if (stopping_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        reconnectRequested_ = true;
    }
    commandWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    for (const std::pair<const std::uint64_t, ClientConnection>& entry : clients_)
        close(entry.second.descriptor);
    clients_.clear();
    if (listener_ >= 0) close(listener_);
    if (wakeRead_ >= 0) close(wakeRead_);
    if (wakeWrite_ >= 0) close(wakeWrite_);
    listener_ = wakeRead_ = wakeWrite_ = -1;
}

void AirCtrlServer::acceptClients() {
    for (;;) {
        const int descriptor = accept(listener_, nullptr, nullptr);
        if (descriptor < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                std::cerr << "AirControl-Server: accept fehlgeschlagen: " << std::strerror(errno) << '\n';
            break;
        }
        if (setNonBlocking(descriptor) != 0) {
            close(descriptor);
            continue;
        }
        const std::uint64_t id = nextClient_++;
        clients_.emplace(id, ClientConnection{descriptor, {}, {}, false});
        acceptedAnyClient_ = true;
        send(id, stateEnvelope());
        if (state_ == "connected" && lastStatus_.is_object() && !lastStatus_.empty())
            send(id, {{"_airctrl", "status"}, {"data", lastStatus_}});
        flushClient(id);
    }
}

void AirCtrlServer::closeClient(std::uint64_t id) {
    const std::unordered_map<std::uint64_t, ClientConnection>::iterator found = clients_.find(id);
    if (found == clients_.end()) return;
    close(found->second.descriptor);
    clients_.erase(found);
    if (exitOnIdle_ && acceptedAnyClient_ && clients_.empty()) quitRequested_ = true;
}

void AirCtrlServer::readClient(std::uint64_t id) {
    std::unordered_map<std::uint64_t, ClientConnection>::iterator found = clients_.find(id);
    if (found == clients_.end()) return;
    char block[16384];
    for (;;) {
        const ssize_t count = recv(found->second.descriptor, block, sizeof(block), 0);
        if (count > 0) {
            found->second.input.append(block, static_cast<std::size_t>(count));
            if (found->second.input.size() > maximumInputSize) {
                send(id, {{"_airctrl", "error"}, {"error", "IPC-Nachricht ist zu groß."}});
                found->second.closeAfterWrite = true;
                flushClient(id);
                return;
            }
            continue;
        }
        if (count == 0) {
            closeClient(id);
            return;
        }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) closeClient(id);
        break;
    }
    found = clients_.find(id);
    if (found == clients_.end()) return;
    for (;;) {
        const std::size_t newline = found->second.input.find('\n');
        if (newline == std::string::npos) break;
        const std::string line = trim(found->second.input.substr(0U, newline));
        found->second.input.erase(0U, newline + 1U);
        if (line.empty()) continue;
        const Json request = Json::parse(line, nullptr, false);
        if (request.is_discarded() || !request.is_object()) {
            send(id, {{"_airctrl", "error"}, {"error", "Ungültiges IPC-JSON."}});
            continue;
        }
        handle(id, request);
    }
    flushClient(id);
}

void AirCtrlServer::handle(std::uint64_t client, const Json& request) {
    const Json::const_iterator kindValue = request.find("_airctrl");
    const std::string kind = kindValue != request.end() && kindValue->is_string()
        ? kindValue->get<std::string>() : std::string{};
    if (kind == "configure") {
        send(client, {{"_airctrl", "error"},
            {"error", "Geräteeinstellungen gehören ausschließlich in /etc/airctrld.cfg."}});
        return;
    }
    if (kind == "refresh") {
        if (refreshScheduled_) return;
        refreshScheduled_ = true;
        deviceReady_.store(false);
        std::deque<DeviceCommand> pending;
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            reconnectRequested_ = true;
            pending.swap(commands_);
        }
        for (DeviceCommand& command : pending)
            postControl(std::move(command), false, "Geräte-I/O wird erneuert; Befehl nicht ausgeführt.");
        commandWake_.notify_all();
        return;
    }
    if (kind == "control") {
        std::uint64_t id = 0;
        const Json::const_iterator valuesValue = request.find("values");
        const Json values = valuesValue == request.end() ? Json::object() : *valuesValue;
        const std::string problem = controlValuesError(values);
        if (!requestId(request, &id) || !problem.empty()) {
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
            std::lock_guard<std::mutex> lock(commandMutex_);
            commands_.push_back({client, id, values});
        }
        commandWake_.notify_all();
        return;
    }
    if (kind == "ping") {
        send(client, {{"_airctrl", "pong"}});
        return;
    }
    send(client, {{"_airctrl", "error"}, {"error", "Unbekannter IPC-Befehl."}});
}

void AirCtrlServer::send(std::uint64_t client, const Json& object) {
    const std::unordered_map<std::uint64_t, ClientConnection>::iterator found = clients_.find(client);
    if (found == clients_.end()) return;
    found->second.output += object.dump();
    found->second.output.push_back('\n');
    if (found->second.output.size() > maximumOutputSize) closeClient(client);
}

void AirCtrlServer::flushClient(std::uint64_t id) {
    const std::unordered_map<std::uint64_t, ClientConnection>::iterator found = clients_.find(id);
    if (found == clients_.end()) return;
    while (!found->second.output.empty()) {
#ifdef MSG_NOSIGNAL
        constexpr int sendFlags = MSG_NOSIGNAL;
#else
        constexpr int sendFlags = 0;
#endif
        const ssize_t count = ::send(found->second.descriptor, found->second.output.data(),
                                     found->second.output.size(), sendFlags);
        if (count > 0) {
            found->second.output.erase(0U, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        closeClient(id);
        return;
    }
    if (found->second.closeAfterWrite) closeClient(id);
}

void AirCtrlServer::broadcast(const Json& object) {
    std::vector<std::uint64_t> ids;
    ids.reserve(clients_.size());
    for (const std::pair<const std::uint64_t, ClientConnection>& entry : clients_) ids.push_back(entry.first);
    for (const std::uint64_t id : ids) send(id, object);
}

Json AirCtrlServer::stateEnvelope() const {
    Json object = {{"_airctrl", "state"}, {"state", state_}, {"starts", starts_}};
    if (!stateError_.empty()) object["error"] = stateError_;
    return object;
}

void AirCtrlServer::postEvent(ServerEvent event) {
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        events_.push_back(std::move(event));
    }
    const char byte = 1;
    const ssize_t ignored = write(wakeWrite_, &byte, 1U);
    static_cast<void>(ignored);
}

void AirCtrlServer::processEvents() {
    char bytes[256];
    while (read(wakeRead_, bytes, sizeof(bytes)) > 0) {}
    std::deque<ServerEvent> pending;
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        pending.swap(events_);
    }
    for (ServerEvent& event : pending) {
        if (event.kind == EventKind::Start) {
            ++starts_;
            state_ = "connecting";
            stateError_.clear();
            broadcast(stateEnvelope());
        } else if (event.kind == EventKind::State) {
            state_ = event.state;
            stateError_ = event.error;
            broadcast(stateEnvelope());
        } else if (event.kind == EventKind::Status) {
            if (!event.data.is_object()) continue;
            lastStatus_ = std::move(event.data);
            state_ = "connected";
            stateError_.clear();
            broadcast({{"_airctrl", "status"}, {"data", lastStatus_}});
        } else {
            Json result = {{"_airctrl", "control"}, {"id", event.id}, {"ok", event.ok}};
            if (!event.error.empty()) result["error"] = event.error;
            send(event.client, result);
        }
    }
}

void AirCtrlServer::postStart() {
    ServerEvent event;
    event.kind = EventKind::Start;
    postEvent(std::move(event));
}

void AirCtrlServer::postState(std::string state, std::string error) {
    ServerEvent event;
    event.kind = EventKind::State;
    event.state = std::move(state);
    event.error = std::move(error);
    postEvent(std::move(event));
}

void AirCtrlServer::postStatus(const Json& status) {
    ServerEvent event;
    event.kind = EventKind::Status;
    event.data = status;
    postEvent(std::move(event));
}

void AirCtrlServer::postControl(DeviceCommand command, bool ok, std::string error) {
    ServerEvent event;
    event.kind = EventKind::Control;
    event.client = command.client;
    event.id = command.id;
    event.ok = ok;
    event.error = std::move(error);
    postEvent(std::move(event));
}

bool AirCtrlServer::interrupted(std::uint64_t generation, bool commandsInterrupt) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    return stopping_.load() || reconnectRequested_ || configGeneration_ != generation ||
           (commandsInterrupt && !commands_.empty());
}

bool AirCtrlServer::waitReconnect(int milliseconds, std::uint64_t generation) {
    std::unique_lock<std::mutex> lock(commandMutex_);
    return commandWake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] {
        return stopping_.load() || reconnectRequested_ || configGeneration_ != generation || !commands_.empty();
    });
}

bool AirCtrlServer::takeCommand(DeviceCommand* command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (commands_.empty()) return false;
    *command = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void AirCtrlServer::deviceLoop() {
    while (!stopping_.load()) {
        DeviceConfig config;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            config = config_.device;
            generation = configGeneration_;
            reconnectRequested_ = false;
        }
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
                    bool reconnect = false;
                    {
                        std::lock_guard<std::mutex> lock(commandMutex_);
                        reconnect = reconnectRequested_ || configGeneration_ != generation;
                    }
                    if (reconnect) break;
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
                bool reconnect = false;
                {
                    std::lock_guard<std::mutex> lock(commandMutex_);
                    reconnect = reconnectRequested_ || configGeneration_ != generation;
                }
                if (reconnect || stopping_.load()) break;
                DeviceCommand command;
                if (takeCommand(&command)) {
                    statusRequiredAfterControl = true;
                    try {
                        const bool accepted = device.set_control_values(command.values, 0, false);
                        bool configurationChanged = false;
                        {
                            std::lock_guard<std::mutex> lock(commandMutex_);
                            configurationChanged = reconnectRequested_ || configGeneration_ != generation;
                        }
                        if (configurationChanged) {
                            postControl(std::move(command), false,
                                "Geräte-I/O wurde während des Schaltbefehls erneuert · Ausgang unbekannt; keine Wiederholung.");
                        } else {
                            postControl(std::move(command), accepted,
                                accepted ? std::string{} : "Gerät hat den Schaltbefehl abgelehnt.");
                        }
                    } catch (const std::exception& exception) {
                        postControl(std::move(command), false, exception.what());
                    }
                }
            }
        } catch (const std::exception& exception) {
            deviceReady_.store(false);
            postState("error", exception.what());
            DeviceCommand command;
            while (takeCommand(&command))
                postControl(std::move(command), false,
                    "Geräte-I/O wurde unterbrochen; Befehl nicht wiederholt.");
            if (!stopping_.load()) waitReconnect(config.reconnectMs, generation);
        }
        deviceReady_.store(false);
    }
}

int runServer(std::string configPath, bool checkConfig) {
    const char* testConfig = std::getenv("AIRCTRL_TEST_SERVER_CONFIG");
    if (testConfig != nullptr && !trim(testConfig).empty()) configPath = trim(testConfig);
    ServerConfig config;
    std::string error;
    if (!loadConfig(configPath, &config, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    if (checkConfig) {
        std::cout << "Konfiguration gültig: " << configPath << '\n';
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, requestTermination);
    signal(SIGTERM, requestTermination);
    AirCtrlServer server(std::move(config));
    if (!server.start(&error)) {
        std::cerr << "AirControl-Server konnte nicht starten: " << error << '\n';
        return 1;
    }
    return server.run();
}

} // namespace airctrl
