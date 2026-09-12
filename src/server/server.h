/**
 * @file server.h
 * @brief Declarations for the Qt-independent AirControl server.
 */
#pragma once

#include <aioairctrl/client.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace airctrl {

using Json = aioairctrl::Json;

struct DeviceConfig {
    std::string host = "AC2729-10";
    int port = 5683;
    int reconnectMs = 10000;
    int requestMs = 60000;
    int idleMs = 90000;
};

struct ServerConfig {
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = 5680;
    DeviceConfig device;
};

struct DeviceCommand {
    std::uint64_t client = 0;
    std::uint64_t id = 0;
    Json values;
};

enum class EventKind { Start, State, Status, Control };

struct ServerEvent {
    EventKind kind = EventKind::State;
    std::uint64_t client = 0;
    std::uint64_t id = 0;
    bool ok = false;
    Json data;
    std::string state;
    std::string error;
};

struct ClientConnection {
    int descriptor = -1;
    std::string input;
    std::string output;
    bool closeAfterWrite = false;
};

/** @brief Bridges many TCP clients to one blocking Philips protocol worker. */
class AirCtrlServer final {
public:
    explicit AirCtrlServer(ServerConfig config);
    ~AirCtrlServer();
    AirCtrlServer(const AirCtrlServer&) = delete;
    AirCtrlServer& operator=(const AirCtrlServer&) = delete;

    bool start(std::string* error);
    int run();

private:
    static bool environmentFlag(const char* name);

    bool openWakePipe(std::string* error);
    bool openListener(std::string* error);
    void shutdown();
    void acceptClients();
    void closeClient(std::uint64_t id);
    void readClient(std::uint64_t id);
    void handle(std::uint64_t client, const Json& request);
    void send(std::uint64_t client, const Json& object);
    void flushClient(std::uint64_t id);
    void broadcast(const Json& object);
    Json stateEnvelope() const;
    void postEvent(ServerEvent event);
    void processEvents();
    void postStart();
    void postState(std::string state, std::string error = {});
    void postStatus(const Json& status);
    void postControl(DeviceCommand command, bool ok, std::string error = {});
    bool interrupted(std::uint64_t generation, bool commandsInterrupt = true);
    bool waitReconnect(int milliseconds, std::uint64_t generation);
    bool takeCommand(DeviceCommand* command);
    void deviceLoop();

    ServerConfig config_;
    const bool exitOnIdle_;
    int listener_ = -1;
    int wakeRead_ = -1;
    int wakeWrite_ = -1;
    std::unordered_map<std::uint64_t, ClientConnection> clients_;
    std::uint64_t nextClient_ = 1;
    std::string state_ = "starting";
    std::string stateError_;
    std::uint64_t starts_ = 0;
    Json lastStatus_ = Json::object();
    bool refreshScheduled_ = false;
    bool acceptedAnyClient_ = false;
    bool quitRequested_ = false;
    std::chrono::steady_clock::time_point initialIdleDeadline_{};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> deviceReady_{false};
    std::mutex commandMutex_;
    std::condition_variable commandWake_;
    std::uint64_t configGeneration_ = 1;
    bool reconnectRequested_ = false;
    std::deque<DeviceCommand> commands_;
    std::thread worker_;
    std::mutex eventMutex_;
    std::deque<ServerEvent> events_;
};

/**
 * Loads the configuration and runs the server until termination is requested.
 *
 * @param configPath Path to the mandatory server configuration.
 * @param checkConfig Validate the configuration and return without starting.
 * @return Process exit status.
 */
int runServer(std::string configPath, bool checkConfig);

} // namespace airctrl
