/**
 * @file automation.h
 * @brief Qt-independent, server-owned Lua automation runtime.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace airctrl {

using Json = nlohmann::json;

struct AutomationConfig {
    std::string scriptPath;
    std::string statePath;
    bool enabledByDefault = false;
};

struct AutomationAction {
    Json values;
    std::string source;
    std::string occurrenceKey;
};

/** Owns the only Lua runtime and automation script in an AirControl setup. */
class AutomationEngine final {
public:
    using ActionHandler = std::function<void(AutomationAction)>;

    AutomationEngine(AutomationConfig config, std::string applicationVersion);
    ~AutomationEngine();
    AutomationEngine(const AutomationEngine&) = delete;
    AutomationEngine& operator=(const AutomationEngine&) = delete;

    /** Create/read the server files and load the script when enabled. */
    bool initialize(std::string* error);
    /** Read the authoritative server copy for a granted editor session. */
    bool readScript(std::string* text, std::string* error) const;
    /** Validate, atomically store and activate a script received from a client. */
    bool saveScript(const std::string& text, bool enabled, std::string* error);
    /** Current state sent to every client; never contains the script text. */
    Json stateJson() const;
    std::uint64_t revision() const { return revision_; }
    const std::string& scriptPath() const { return config_.scriptPath; }

    void setActionHandler(ActionHandler handler) { actionHandler_ = std::move(handler); }
    void setConnected(bool connected, const std::string& reason = {});
    void statusEvent(const Json& status);
    void commandEvent(const std::string& source, bool ok, const std::string& message);
    void processTime(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
    void actionAccepted(const std::string& occurrenceKey);

private:
    struct Schedule {
        std::string name;
        std::string phase;
        int hour = 0;
        int minute = 0;
        std::set<int> days;
        Json values;
        bool catchUp = true;
    };
    struct PendingAction {
        Json values;
        std::string source;
        std::string occurrenceKey;
    };
    struct MemoryLimit {
        std::size_t used = 0;
        std::size_t maximum = 8U * 1024U * 1024U;
    };

    static void* allocator(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize);
    static void instructionHook(lua_State* state, lua_Debug* debug);
    static AutomationEngine* fromLua(lua_State* state);
    static int luaLog(lua_State* state);
    static int luaSet(lua_State* state);
    static int luaSchedule(lua_State* state);
    static int luaStatus(lua_State* state);

    bool validateScript(const std::string& text, std::string* error) const;
    bool loadScriptText(const std::string& text, const std::string& sourceName,
                        bool dispatchInitialEvents);
    bool loadPersistentState(std::string* error);
    bool savePersistentState(std::string* error) const;
    void closeState();
    void openSandbox();
    void registerApi();
    void setProblem(const std::string& problem);
    void appendLog(const std::string& level, const std::string& message);
    bool callEvent(const std::string& type, Json detail = Json::object());
    void flushActions();
    void evaluateSchedules(std::chrono::system_clock::time_point now);
    std::string occurrenceFor(const Schedule& schedule,
                              std::chrono::system_clock::time_point now,
                              std::chrono::system_clock::time_point* when) const;
    void publishDeviceAlerts(const Json& status);

    AutomationConfig config_;
    std::string applicationVersion_;
    lua_State* state_ = nullptr;
    MemoryLimit memory_;
    bool enabled_ = false;
    bool connected_ = false;
    bool dispatching_ = false;
    bool validating_ = false;
    int instructionBudget_ = 200000;
    std::uint64_t revision_ = 1;
    std::string currentEvent_;
    std::string lastError_;
    std::string lastEvent_;
    std::string lastAction_;
    std::string lastMinute_;
    std::string lastAlertDigest_;
    Json latestStatus_ = Json::object();
    std::vector<Schedule> schedules_;
    std::vector<PendingAction> pendingActions_;
    std::vector<std::string> handledOccurrences_;
    std::vector<std::string> logEntries_;
    ActionHandler actionHandler_;
};

} // namespace airctrl
