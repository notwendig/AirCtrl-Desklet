/**
 * @file automation.cpp
 * @brief Resource-limited Lua automation executed exclusively by airctrl-server.
 */
#include "automation.h"

#include "airctrl_automation_example.hpp"
#include "airctrl_version.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <system_error>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace airctrl {
namespace {
constexpr std::size_t maximumScriptSize = 256U * 1024U;
char engineRegistryKey;

std::string cleanLine(std::string text, std::size_t maximum = 1000U) {
    if (text.size() > maximum) text.resize(maximum);
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}

std::tm localTime(std::time_t value) {
    std::tm result{};
    localtime_r(&value, &result);
    return result;
}

std::string formatLocal(std::time_t value, const char* format) {
    const std::tm local = localTime(value);
    char buffer[80]{};
    if (std::strftime(buffer, sizeof(buffer), format, &local) == 0) return {};
    std::string text(buffer);
    if (std::strcmp(format, "%Y-%m-%dT%H:%M:%S%z") == 0 && text.size() >= 5U) {
        const std::size_t offset = text.size() - 5U;
        text.insert(offset + 3U, ":");
    }
    return text;
}

Json timeDetail(std::chrono::system_clock::time_point now) {
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    const std::tm local = localTime(value);
    const int weekday = local.tm_wday == 0 ? 7 : local.tm_wday;
    return {{"iso", formatLocal(value, "%Y-%m-%dT%H:%M:%S%z")},
            {"date", formatLocal(value, "%Y-%m-%d")},
            {"time", formatLocal(value, "%H:%M")},
            {"year", local.tm_year + 1900}, {"month", local.tm_mon + 1},
            {"day", local.tm_mday}, {"weekday", weekday},
            {"hour", local.tm_hour}, {"minute", local.tm_min}};
}

void pushJson(lua_State* state, const Json& value) {
    if (value.is_null()) lua_pushnil(state);
    else if (value.is_boolean()) lua_pushboolean(state, value.get<bool>());
    else if (value.is_number_integer()) lua_pushinteger(state, value.get<lua_Integer>());
    else if (value.is_number_unsigned()) lua_pushinteger(state, static_cast<lua_Integer>(value.get<std::uint64_t>()));
    else if (value.is_number_float()) lua_pushnumber(state, value.get<lua_Number>());
    else if (value.is_string()) {
        const std::string text = value.get<std::string>();
        lua_pushlstring(state, text.data(), text.size());
    } else if (value.is_array()) {
        lua_createtable(state, static_cast<int>(value.size()), 0);
        std::size_t index = 1;
        for (const Json& item : value) {
            pushJson(state, item);
            lua_rawseti(state, -2, static_cast<lua_Integer>(index++));
        }
    } else {
        lua_createtable(state, 0, static_cast<int>(value.size()));
        for (Json::const_iterator item = value.begin(); item != value.end(); ++item) {
            pushJson(state, item.value());
            lua_setfield(state, -2, item.key().c_str());
        }
    }
}

std::string luaString(lua_State* state, int index) {
    std::size_t length = 0;
    const char* text = lua_tolstring(state, index, &length);
    return text == nullptr ? std::string{} : std::string(text, length);
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

Json simpleTable(lua_State* state, int index, std::string* error) {
    Json result = Json::object();
    const int absolute = lua_absindex(state, index);
    if (!lua_istable(state, absolute)) {
        *error = "Tabelle mit Steuerwerten erwartet.";
        return {};
    }
    lua_pushnil(state);
    while (lua_next(state, absolute) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            *error = "Steuerfelder müssen Textschlüssel haben.";
            lua_pop(state, 2);
            return {};
        }
        const std::string key = luaString(state, -2);
        switch (lua_type(state, -1)) {
        case LUA_TBOOLEAN: result[key] = lua_toboolean(state, -1) != 0; break;
        case LUA_TSTRING: result[key] = luaString(state, -1); break;
        case LUA_TNUMBER:
            if (!lua_isinteger(state, -1)) {
                *error = "Steuerzahlen müssen ganzzahlig sein: " + key;
                lua_pop(state, 2);
                return {};
            }
            result[key] = lua_tointeger(state, -1);
            break;
        default:
            *error = "Unzulässiger Lua-Wert für Steuerfeld: " + key;
            lua_pop(state, 2);
            return {};
        }
        lua_pop(state, 1);
    }
    *error = controlValuesError(result);
    return error->empty() ? result : Json{};
}

bool writeAtomic(const std::string& path, const std::string& bytes, std::string* error) {
    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code filesystemError;
    if (!target.has_parent_path() ||
        (!fs::create_directories(target.parent_path(), filesystemError) && filesystemError)) {
        if (error) *error = "Serverordner kann nicht angelegt werden: " + filesystemError.message();
        return false;
    }
    const std::string temporary = path + ".tmp." + std::to_string(static_cast<long long>(getpid()));
    const int descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        if (error) *error = "Temporäre Serverdatei kann nicht angelegt werden: " + std::string(std::strerror(errno));
        return false;
    }
    bool ok = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t count = write(descriptor, bytes.data() + written, bytes.size() - written);
        if (count > 0) written += static_cast<std::size_t>(count);
        else if (count < 0 && errno == EINTR) continue;
        else { ok = false; break; }
    }
    int savedError = 0;
    if (ok && fsync(descriptor) != 0) { ok = false; savedError = errno; }
    if (close(descriptor) != 0 && ok) { ok = false; savedError = errno; }
    if (ok && rename(temporary.c_str(), path.c_str()) != 0) { ok = false; savedError = errno; }
    if (!ok) {
        unlink(temporary.c_str());
        if (error) *error = "Serverdatei kann nicht atomar gespeichert werden: " +
            std::string(std::strerror(savedError == 0 ? EIO : savedError));
        return false;
    }
    return true;
}

bool integerValue(const Json& value, int* result) {
    try {
        double number = 0.0;
        if (value.is_number()) number = value.get<double>();
        else if (value.is_string()) {
            const std::string text = value.get<std::string>();
            std::size_t used = 0;
            number = std::stod(text, &used);
            if (used != text.size()) return false;
        } else return false;
        if (!std::isfinite(number) || number != std::floor(number) ||
            number < -1000000.0 || number > 1000000.0) return false;
        *result = static_cast<int>(number);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool isAc2729(const Json& status) {
    const auto upper = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char byte) { return static_cast<char>(std::toupper(byte)); });
        return text;
    };
    const Json::const_iterator model = status.find("modelid");
    if (model != status.end() && model->is_string() && !model->get_ref<const std::string&>().empty()) {
        const std::string value = upper(model->get<std::string>());
        return value == "AC2729" || value.rfind("AC2729/", 0) == 0;
    }
    const Json::const_iterator type = status.find("type");
    return type != status.end() && type->is_string() && upper(type->get<std::string>()) == "AC2729";
}
} // namespace

AutomationEngine::AutomationEngine(AutomationConfig config, std::string applicationVersion)
    : config_(std::move(config)), applicationVersion_(std::move(applicationVersion)) {}

AutomationEngine::~AutomationEngine() { closeState(); }

bool AutomationEngine::initialize(std::string* error) {
    if (!loadPersistentState(error)) return false;
    std::string script;
    if (!readScript(&script, error)) {
        if (std::filesystem::exists(config_.scriptPath)) return false;
        script = AirctrlAutomationExample;
        if (!writeAtomic(config_.scriptPath, script, error)) return false;
    }
    std::string validationError;
    if (!validateScript(script, &validationError)) {
        setProblem(validationError);
        if (enabled_) {
            if (error) *error = validationError;
            return false;
        }
        return true;
    }
    if (enabled_ && !loadScriptText(script, config_.scriptPath, true)) {
        if (error) *error = lastError_;
        return false;
    }
    return true;
}

bool AutomationEngine::readScript(std::string* text, std::string* error) const {
    std::ifstream file(config_.scriptPath, std::ios::binary);
    if (!file) {
        if (error) *error = "Lua-Skript auf dem Server kann nicht gelesen werden: " + config_.scriptPath;
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximumScriptSize) {
        if (error) *error = "Lua-Skript auf dem Server ist größer als 256 KiB.";
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::ostringstream content;
    content << file.rdbuf();
    if (!file.good() && !file.eof()) {
        if (error) *error = "Lua-Skript auf dem Server kann nicht vollständig gelesen werden.";
        return false;
    }
    *text = content.str();
    return true;
}

bool AutomationEngine::saveScript(const std::string& text, bool enabled, std::string* error) {
    if (text.size() > maximumScriptSize) {
        if (error) *error = "Lua-Skript ist größer als 256 KiB.";
        return false;
    }
    std::string validationError;
    if (!validateScript(text, &validationError)) {
        if (error) *error = validationError;
        return false;
    }
    std::string previousScript;
    std::string readError;
    const bool hadPreviousScript = readScript(&previousScript, &readError);
    const bool previousEnabled = enabled_;
    const std::uint64_t previousRevision = revision_;
    if (!writeAtomic(config_.scriptPath, text, error)) return false;
    enabled_ = enabled;
    ++revision_;
    if (!savePersistentState(error)) {
        enabled_ = previousEnabled;
        revision_ = previousRevision;
        if (hadPreviousScript) {
            std::string ignored;
            writeAtomic(config_.scriptPath, previousScript, &ignored);
        }
        return false;
    }
    if (enabled_) {
        if (!loadScriptText(text, config_.scriptPath, true)) {
            if (error) *error = lastError_;
            return false;
        }
    } else {
        closeState();
        lastError_.clear();
        lastEvent_ = "deaktiviert";
    }
    return true;
}

Json AutomationEngine::stateJson() const {
    std::set<std::string> ruleNames;
    for (const Schedule& schedule : schedules_) ruleNames.insert(schedule.name);
    Json result = {{"enabled", enabled_}, {"loaded", state_ != nullptr && lastError_.empty()},
                   {"manual_override", manualOverride_},
                   {"revision", revision_}, {"schedule_count", ruleNames.size()},
                   {"lua_version", LUA_RELEASE}, {"script_path", config_.scriptPath},
                   {"last_event", lastEvent_}, {"last_action", lastAction_}};
    if (!lastError_.empty()) result["error"] = lastError_;
    if (manualOverride_ && !manualOverrideReason_.empty())
        result["manual_override_reason"] = manualOverrideReason_;
    result["log"] = logEntries_;
    return result;
}

bool AutomationEngine::loadPersistentState(std::string* error) {
    enabled_ = config_.enabledByDefault;
    std::ifstream file(config_.statePath, std::ios::binary);
    if (!file) return true;
    try {
        Json state;
        file >> state;
        if (!state.is_object()) throw std::runtime_error("JSON-Objekt erwartet");
        if (state.contains("enabled") && state["enabled"].is_boolean()) enabled_ = state["enabled"].get<bool>();
        if (state.contains("manual_override") && state["manual_override"].is_boolean())
            manualOverride_ = state["manual_override"].get<bool>();
        if (state.contains("manual_override_reason") && state["manual_override_reason"].is_string())
            manualOverrideReason_ = cleanLine(state["manual_override_reason"].get<std::string>(), 200U);
        if (state.contains("revision") && state["revision"].is_number_unsigned())
            revision_ = std::max<std::uint64_t>(1U, state["revision"].get<std::uint64_t>());
        if (state.contains("handled_occurrences") && state["handled_occurrences"].is_array()) {
            for (const Json& item : state["handled_occurrences"])
                if (item.is_string() && handledOccurrences_.size() < 64U)
                    handledOccurrences_.push_back(item.get<std::string>());
        }
    } catch (const std::exception& exception) {
        if (error) *error = "Lua-Zustandsdatei ist ungültig: " + std::string(exception.what());
        return false;
    }
    return true;
}

bool AutomationEngine::savePersistentState(std::string* error) const {
    const Json state = {{"enabled", enabled_}, {"manual_override", manualOverride_},
                        {"manual_override_reason", manualOverrideReason_}, {"revision", revision_},
                        {"handled_occurrences", handledOccurrences_}};
    return writeAtomic(config_.statePath, state.dump(2) + "\n", error);
}

bool AutomationEngine::validateScript(const std::string& text, std::string* error) const {
    AutomationEngine validator(config_, applicationVersion_);
    validator.enabled_ = true;
    validator.validating_ = true;
    if (!validator.loadScriptText(text, "Editorprüfung", true)) {
        *error = validator.lastError_;
        return false;
    }
    error->clear();
    return true;
}

void* AutomationEngine::allocator(void* userData, void* pointer, std::size_t oldSize,
                                  std::size_t newSize) {
    MemoryLimit* memory = static_cast<MemoryLimit*>(userData);
    if (newSize == 0U) {
        std::free(pointer);
        memory->used = oldSize > memory->used ? 0U : memory->used - oldSize;
        return nullptr;
    }
    const std::size_t base = pointer != nullptr
        ? (oldSize > memory->used ? 0U : memory->used - oldSize) : memory->used;
    if (newSize > memory->maximum || base > memory->maximum - newSize) return nullptr;
    void* result = std::realloc(pointer, newSize);
    if (result != nullptr) memory->used = base + newSize;
    return result;
}

void AutomationEngine::instructionHook(lua_State* state, lua_Debug*) {
    luaL_error(state, "Ausführungslimit der Lua-Automatik überschritten");
}

AutomationEngine* AutomationEngine::fromLua(lua_State* state) {
    lua_pushlightuserdata(state, &engineRegistryKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    AutomationEngine* result = static_cast<AutomationEngine*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return result;
}

void AutomationEngine::closeState() {
    pendingActions_.clear();
    schedules_.clear();
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
    memory_ = {};
}

void AutomationEngine::openSandbox() {
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {nullptr, nullptr}};
    for (const luaL_Reg* library = libraries; library->func != nullptr; ++library) {
        luaL_requiref(state_, library->name, library->func, 1);
        lua_pop(state_, 1);
    }
    for (const char* name : {"dofile", "loadfile", "load"}) {
        lua_pushnil(state_);
        lua_setglobal(state_, name);
    }
}

void AutomationEngine::registerApi() {
    lua_pushlightuserdata(state_, &engineRegistryKey);
    lua_pushlightuserdata(state_, this);
    lua_settable(state_, LUA_REGISTRYINDEX);
    lua_createtable(state_, 0, 6);
    lua_pushcfunction(state_, &AutomationEngine::luaLog); lua_setfield(state_, -2, "log");
    lua_pushcfunction(state_, &AutomationEngine::luaSet); lua_setfield(state_, -2, "set");
    lua_pushcfunction(state_, &AutomationEngine::luaSchedule); lua_setfield(state_, -2, "schedule");
    lua_pushcfunction(state_, &AutomationEngine::luaStatus); lua_setfield(state_, -2, "status");
    lua_pushlstring(state_, applicationVersion_.data(), applicationVersion_.size());
    lua_setfield(state_, -2, "version");
    lua_pushstring(state_, LUA_RELEASE); lua_setfield(state_, -2, "lua_version");
    lua_setglobal(state_, "airctrl");
    lua_pushcfunction(state_, &AutomationEngine::luaLog);
    lua_setglobal(state_, "print");
}

bool AutomationEngine::loadScriptText(const std::string& text, const std::string& sourceName,
                                      bool dispatchInitialEvents) {
    closeState();
    lastError_.clear();
    lastEvent_.clear();
    lastAction_.clear();
    lastMinute_.clear();
    lastAlertDigest_.clear();
    state_ = lua_newstate(&AutomationEngine::allocator, &memory_);
    if (state_ == nullptr) {
        setProblem("Lua konnte wegen des Speicherlimits nicht gestartet werden.");
        return false;
    }
    openSandbox();
    registerApi();
    int result = luaL_loadbufferx(state_, text.data(), text.size(), sourceName.c_str(), "t");
    if (result == LUA_OK) {
        lua_sethook(state_, &AutomationEngine::instructionHook, LUA_MASKCOUNT, instructionBudget_);
        dispatching_ = true;
        result = lua_pcall(state_, 0, 0, 0);
        dispatching_ = false;
        lua_sethook(state_, nullptr, 0, 0);
    }
    if (result != LUA_OK) {
        const std::string problem = "Lua-Skriptfehler: " + luaString(state_, -1);
        lua_pop(state_, 1);
        closeState();
        setProblem(problem);
        return false;
    }
    lastEvent_ = "Skript geladen";
    if (dispatchInitialEvents) {
        callEvent("startup", {{"time", timeDetail(std::chrono::system_clock::now())}});
        processTime();
    }
    return state_ != nullptr && lastError_.empty();
}

void AutomationEngine::setProblem(const std::string& problem) {
    lastError_ = problem;
    appendLog("error", problem);
}

void AutomationEngine::appendLog(const std::string& level, const std::string& message) {
    const std::time_t now = std::time(nullptr);
    const std::string entry = formatLocal(now, "%Y-%m-%dT%H:%M:%S%z") + " [" + level + "] " +
        cleanLine(message);
    logEntries_.push_back(entry);
    if (logEntries_.size() > 50U) logEntries_.erase(logEntries_.begin());
    if (!validating_) std::clog << "AirControl Lua: " << entry << '\n';
}

int AutomationEngine::luaLog(lua_State* state) {
    AutomationEngine* self = fromLua(state);
    if (self == nullptr) return 0;
    std::string level = "info";
    std::string message;
    if (lua_gettop(state) >= 2) {
        level = luaL_checkstring(state, 1);
        message = luaL_checkstring(state, 2);
    } else message = luaL_checkstring(state, 1);
    if (level != "debug" && level != "info" && level != "warning" && level != "error") level = "info";
    self->appendLog(level, message);
    return 0;
}

int AutomationEngine::luaSet(lua_State* state) {
    AutomationEngine* self = fromLua(state);
    if (self == nullptr) return 0;
    if (self->currentEvent_ != "connected" && self->currentEvent_ != "status" &&
        self->currentEvent_ != "alarm" && self->currentEvent_ != "time" &&
        self->currentEvent_ != "long_timer")
        return luaL_error(state,
            "airctrl.set ist nur in connected-, status-, alarm-, time- oder long_timer-Ereignissen erlaubt");
    if (!self->pendingActions_.empty())
        return luaL_error(state, "pro Ereignis ist nur ein airctrl.set-Auftrag erlaubt");
    std::string error;
    const Json values = simpleTable(state, 1, &error);
    if (!error.empty()) return luaL_error(state, "%s", error.c_str());
    self->pendingActions_.push_back({values, "Lua-Ereignis " + self->currentEvent_, {}});
    return 0;
}

int AutomationEngine::luaSchedule(lua_State* state) {
    AutomationEngine* self = fromLua(state);
    if (self == nullptr) return 0;
    luaL_checktype(state, 1, LUA_TTABLE);
    const int table = lua_absindex(state, 1);
    const auto fieldString = [&](const char* name) {
        lua_getfield(state, table, name);
        const std::string result = luaString(state, -1);
        lua_pop(state, 1);
        return result;
    };
    const std::string name = fieldString("name");
    const std::string at = fieldString("at");
    const std::string between = fieldString("between");
    static const std::regex validName("^[A-Za-z0-9_./-]{1,64}$");
    static const std::regex validTime("^([01][0-9]|2[0-3]):[0-5][0-9]$");
    static const std::regex validRange(
        "^([01][0-9]|2[0-3]):[0-5][0-9]-([01][0-9]|2[0-3]):[0-5][0-9]$");
    if (!std::regex_match(name, validName))
        return luaL_error(state, "schedule.name: 1-64 Zeichen aus A-Z, a-z, 0-9, _./- erwartet");
    if (at.empty() == between.empty())
        return luaL_error(state, "schedule: genau at oder between angeben");
    if (!at.empty() && !std::regex_match(at, validTime))
        return luaL_error(state, "schedule.at: HH:MM erwartet");
    if (!between.empty() && !std::regex_match(between, validRange))
        return luaL_error(state, "schedule.between: HH:MM-HH:MM erwartet");
    std::set<std::string> existingNames;
    for (const Schedule& existing : self->schedules_) existingNames.insert(existing.name);
    if (existingNames.size() >= 64U) return luaL_error(state, "höchstens 64 Zeitpläne erlaubt");
    for (const Schedule& existing : self->schedules_)
        if (existing.name == name)
            return luaL_error(state, "doppelter Zeitplanname: %s", name.c_str());
    std::set<int> days;
    lua_getfield(state, table, "days");
    if (lua_isnil(state, -1)) {
        for (int day = 1; day <= 7; ++day) days.insert(day);
    } else {
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            return luaL_error(state, "schedule.days: Tabelle erwartet");
        }
        const std::size_t count = lua_rawlen(state, -1);
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
            const int day = static_cast<int>(luaL_checkinteger(state, -1));
            lua_pop(state, 1);
            if (day < 1 || day > 7) {
                lua_pop(state, 1);
                return luaL_error(state, "schedule.days: Wochentage 1 bis 7 erwartet");
            }
            days.insert(day);
        }
        if (days.empty()) {
            lua_pop(state, 1);
            return luaL_error(state, "schedule.days darf nicht leer sein");
        }
    }
    lua_pop(state, 1);
    bool catchUp = true;
    lua_getfield(state, table, "catch_up");
    if (!lua_isnil(state, -1) && !lua_isboolean(state, -1)) {
        lua_pop(state, 1);
        return luaL_error(state, "schedule.catch_up: Boolean erwartet");
    }
    if (!lua_isnil(state, -1)) catchUp = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    const auto fieldValues = [&](const char* field, Json* values) {
        lua_getfield(state, table, field);
        std::string error;
        *values = simpleTable(state, -1, &error);
        lua_pop(state, 1);
        return error;
    };
    Json insideValues;
    const std::string setError = fieldValues("set", &insideValues);
    if (!setError.empty()) return luaL_error(state, "schedule.set: %s", setError.c_str());

    const auto makeSchedule = [&](const std::string& time, std::set<int> activeDays,
                                  Json values, std::string phase) {
        Schedule result;
        result.name = name;
        result.phase = std::move(phase);
        result.hour = std::stoi(time.substr(0, 2));
        result.minute = std::stoi(time.substr(3, 2));
        result.days = std::move(activeDays);
        result.values = std::move(values);
        result.catchUp = catchUp;
        return result;
    };
    std::vector<Schedule> additions;
    if (!at.empty()) {
        lua_getfield(state, table, "outside");
        const bool hasOutside = !lua_isnil(state, -1);
        lua_pop(state, 1);
        if (hasOutside) return luaL_error(state, "schedule.outside ist nur mit between erlaubt");
        additions.push_back(makeSchedule(at, days, std::move(insideValues), {}));
    } else {
        const std::string start = between.substr(0, 5);
        const std::string end = between.substr(6, 5);
        if (start == end) return luaL_error(state, "schedule.between darf nicht bei derselben Uhrzeit enden");
        Json outsideValues;
        const std::string outsideError = fieldValues("outside", &outsideValues);
        if (!outsideError.empty())
            return luaL_error(state, "schedule.outside: %s", outsideError.c_str());
        std::set<int> endDays = days;
        if (end < start) {
            endDays.clear();
            for (const int day : days) endDays.insert(day == 7 ? 1 : day + 1);
        }
        additions.push_back(makeSchedule(start, days, std::move(insideValues), "Beginn"));
        additions.push_back(makeSchedule(end, std::move(endDays), std::move(outsideValues), "Ende"));
    }
    const auto label = [](const Schedule& schedule) {
        return schedule.phase.empty() ? schedule.name :
            schedule.name + " (" + schedule.phase + ")";
    };
    for (std::size_t index = 0; index < additions.size(); ++index) {
        const Schedule& addition = additions[index];
        const auto conflicts = [&](const Schedule& existing) {
            std::vector<int> overlap;
            std::set_intersection(existing.days.begin(), existing.days.end(),
                addition.days.begin(), addition.days.end(), std::back_inserter(overlap));
            return existing.hour == addition.hour && existing.minute == addition.minute &&
                !overlap.empty();
        };
        for (const Schedule& existing : self->schedules_)
            if (conflicts(existing))
                return luaL_error(state, "Zeitpläne %s und %s überschneiden sich zur selben Uhrzeit",
                    label(existing).c_str(), label(addition).c_str());
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            if (conflicts(additions[earlier]))
                return luaL_error(state, "Zeitplan %s besitzt zwei gleiche Schaltzeitpunkte",
                    name.c_str());
    }
    self->schedules_.insert(self->schedules_.end(),
        std::make_move_iterator(additions.begin()), std::make_move_iterator(additions.end()));
    return 0;
}

int AutomationEngine::luaStatus(lua_State* state) {
    AutomationEngine* self = fromLua(state);
    if (self == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    pushJson(state, self->latestStatus_);
    return 1;
}

bool AutomationEngine::callEvent(const std::string& type, Json detail) {
    if (state_ == nullptr || dispatching_ || !lastError_.empty() || manualOverride_) return false;
    lua_getglobal(state_, "on_event");
    if (lua_isnil(state_, -1)) {
        lua_pop(state_, 1);
        return true;
    }
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 1);
        setProblem("Lua: on_event ist keine Funktion.");
        return false;
    }
    detail["type"] = type;
    detail["timestamp"] = formatLocal(std::time(nullptr), "%Y-%m-%dT%H:%M:%S%z");
    pushJson(state_, detail);
    currentEvent_ = type;
    dispatching_ = true;
    lua_sethook(state_, &AutomationEngine::instructionHook, LUA_MASKCOUNT, instructionBudget_);
    const int result = lua_pcall(state_, 1, 0, 0);
    lua_sethook(state_, nullptr, 0, 0);
    dispatching_ = false;
    currentEvent_.clear();
    if (result != LUA_OK) {
        const std::string problem = "Lua-Ereignis " + type + ": " + luaString(state_, -1);
        lua_pop(state_, 1);
        pendingActions_.clear();
        setProblem(problem);
        return false;
    }
    lastEvent_ = type + " · " + formatLocal(std::time(nullptr), "%Y-%m-%dT%H:%M:%S%z");
    flushActions();
    return true;
}

bool AutomationEngine::longTimerEvent() {
    // A deliberate long press is an explicit user action. It remains available
    // while scheduled/status automation is suspended by a manual override.
    if (state_ == nullptr || dispatching_ || !lastError_.empty()) return false;
    lua_getglobal(state_, "on_long_timer");
    if (lua_isnil(state_, -1)) {
        lua_pop(state_, 1);
        lastEvent_ = "long_timer · keine Funktion";
        return true;
    }
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 1);
        setProblem("Lua: on_long_timer ist keine Funktion.");
        return false;
    }
    currentEvent_ = "long_timer";
    dispatching_ = true;
    lua_sethook(state_, &AutomationEngine::instructionHook, LUA_MASKCOUNT, instructionBudget_);
    const int result = lua_pcall(state_, 0, 0, 0);
    lua_sethook(state_, nullptr, 0, 0);
    dispatching_ = false;
    currentEvent_.clear();
    if (result != LUA_OK) {
        const std::string problem = "Lua-Funktion on_long_timer: " + luaString(state_, -1);
        lua_pop(state_, 1);
        pendingActions_.clear();
        setProblem(problem);
        return false;
    }
    lastEvent_ = "long_timer · " + formatLocal(std::time(nullptr), "%Y-%m-%dT%H:%M:%S%z");
    flushActions();
    return true;
}

void AutomationEngine::flushActions() {
    std::vector<PendingAction> actions;
    actions.swap(pendingActions_);
    if (!actionHandler_) return;
    for (PendingAction& action : actions)
        actionHandler_({std::move(action.values), std::move(action.source),
                        std::move(action.occurrenceKey)});
}

bool AutomationEngine::setManualOverride(bool active, const std::string& reason) {
    if (manualOverride_ == active) return false;
    manualOverride_ = active;
    manualOverrideReason_ = active ? cleanLine(reason, 200U) : std::string{};
    lastEvent_ = active ? "Automatik manuell gesperrt" : "Automatik-Sperre aufgehoben";
    appendLog("info", active
        ? "Lua-Automatik durch eine manuelle Geräteeinstellung gesperrt."
        : "Manuelle Automatik-Sperre aufgehoben; Skript wird neu ausgewertet.");
    std::string error;
    if (!savePersistentState(&error)) appendLog("error", error);
    return true;
}

void AutomationEngine::reevaluate(const Json& status,
                                  std::chrono::system_clock::time_point now) {
    if (manualOverride_ || state_ == nullptr || !lastError_.empty()) return;
    latestStatus_ = status;
    Json changed = Json::object();
    for (Json::const_iterator item = status.begin(); item != status.end(); ++item)
        changed[item.key()] = {{"old", item.value()}, {"new", item.value()}};
    callEvent("status", {{"status", status}, {"changed", changed},
                         {"first", false}, {"reevaluation", true}});
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    lastMinute_ = formatLocal(value, "%Y-%m-%dT%H:%M");
    callEvent("time", timeDetail(now));
    evaluateSchedules(now, true);
}

void AutomationEngine::setConnected(bool connected, const std::string& reason) {
    if (connected_ == connected) return;
    connected_ = connected;
    Json detail = Json::object();
    if (!reason.empty()) detail["reason"] = reason;
    callEvent(connected ? "connected" : "disconnected", std::move(detail));
    if (connected_) evaluateSchedules(std::chrono::system_clock::now());
}

void AutomationEngine::statusEvent(const Json& status) {
    Json changed = Json::object();
    for (Json::const_iterator item = status.begin(); item != status.end(); ++item) {
        const Json::const_iterator old = latestStatus_.find(item.key());
        if (old == latestStatus_.end() || *old != item.value())
            changed[item.key()] = {{"old", old == latestStatus_.end() ? Json{} : *old},
                                   {"new", item.value()}};
    }
    for (Json::const_iterator item = latestStatus_.begin(); item != latestStatus_.end(); ++item)
        if (!status.contains(item.key())) changed[item.key()] = {{"old", item.value()}, {"new", nullptr}};
    const bool first = latestStatus_.empty();
    latestStatus_ = status;
    if (!connected_) setConnected(true);
    if (manualOverride_) return;
    callEvent("status", {{"status", status}, {"changed", changed}, {"first", first}});
    publishDeviceAlerts(status);
    evaluateSchedules(std::chrono::system_clock::now());
}

void AutomationEngine::publishDeviceAlerts(const Json& status) {
    Json alerts = Json::array();
    if (isAc2729(status) && status.value("pwr", std::string{}) == "1") {
        const struct Filter { const char* tag; const char* id; const char* label; } filters[] = {
            {"fltsts1", "device-filter-hepa", "A3 · HEPA-Filter"},
            {"fltsts2", "device-filter-carbon", "C7 · Aktivkohlefilter"},
            {"wicksts", "device-filter-wick", "F1 · Befeuchtungsdocht"}};
        for (const Filter& filter : filters) {
            const Json::const_iterator found = status.find(filter.tag);
            int hours = 0;
            if (found == status.end() || !integerValue(*found, &hours) || hours < 0 || hours > 120) continue;
            alerts.push_back({{"id", filter.id}, {"level", hours == 0 ? "error" : "warning"},
                {"message", std::string(hours == 0 ? "Filterwechsel fällig: " :
                    "Filterwechsel vorbereiten: ") + filter.label + " · " + std::to_string(hours) +
                    " Betriebsstunden Rest (" + filter.tag + ")"}});
        }
        const Json::const_iterator function = status.find("func");
        const Json::const_iterator water = status.find("wl");
        const Json::const_iterator error = status.find("err");
        int waterValue = -1;
        int errorValue = -1;
        if (function != status.end() && function->is_string() && *function == "PH" &&
            ((water != status.end() && integerValue(*water, &waterValue) && waterValue == 0) ||
             (error != status.end() && integerValue(*error, &errorValue) && errorValue == 49408)))
            alerts.push_back({{"id", "device-water"}, {"level", "warning"},
                {"message", "Wasser nachfüllen: wl=0 oder bekannter Leerstandscode 49408"}});
        const Json::const_iterator prefilter = status.find("fltsts0");
        int prefilterValue = -1;
        if ((prefilter != status.end() && integerValue(*prefilter, &prefilterValue) && prefilterValue == 0) ||
            (error != status.end() && integerValue(*error, &errorValue) &&
             (errorValue == 49153 || errorValue == 49155)))
            alerts.push_back({{"id", "device-clean"}, {"level", "warning"},
                {"message", "Reinigung fällig: Vorfilter / Befeuchtungselement"}});
    }
    std::vector<std::string> digestParts;
    std::string highest = "none";
    for (const Json& alert : alerts) {
        const std::string level = alert.value("level", std::string("warning"));
        digestParts.push_back(alert.value("id", std::string("unknown")) + ":" + level);
        if (level == "error") highest = "error";
        else if (highest == "none") highest = "warning";
    }
    std::sort(digestParts.begin(), digestParts.end());
    std::ostringstream digest;
    for (const std::string& part : digestParts) digest << part << '|';
    if (digest.str() == lastAlertDigest_) return;
    lastAlertDigest_ = digest.str();
    callEvent("alarm", {{"alerts", alerts}, {"count", alerts.size()}, {"highest", highest}});
}

void AutomationEngine::commandEvent(const std::string& source, bool ok,
                                    const std::string& message) {
    lastAction_ = source + " · " + (ok ? "bestätigt" : "fehlgeschlagen") + " · " +
        formatLocal(std::time(nullptr), "%Y-%m-%dT%H:%M:%S%z");
    callEvent("command", {{"source", source}, {"ok", ok}, {"message", message}});
}

std::string AutomationEngine::occurrenceFor(const Schedule& schedule,
    std::chrono::system_clock::time_point now,
    std::chrono::system_clock::time_point* when) const {
    const std::time_t nowValue = std::chrono::system_clock::to_time_t(now);
    const std::tm nowLocal = localTime(nowValue);
    for (int back = 0; back <= 7; ++back) {
        std::tm candidate = nowLocal;
        candidate.tm_mday -= back;
        candidate.tm_hour = schedule.hour;
        candidate.tm_min = schedule.minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        const std::time_t candidateValue = std::mktime(&candidate);
        if (candidateValue == static_cast<std::time_t>(-1) || candidateValue > nowValue) continue;
        const std::tm normalized = localTime(candidateValue);
        const int weekday = normalized.tm_wday == 0 ? 7 : normalized.tm_wday;
        if (schedule.days.find(weekday) == schedule.days.end()) continue;
        if (!schedule.catchUp &&
            (normalized.tm_year != nowLocal.tm_year || normalized.tm_yday != nowLocal.tm_yday ||
             normalized.tm_hour != nowLocal.tm_hour || normalized.tm_min != nowLocal.tm_min)) return {};
        if (when != nullptr) *when = std::chrono::system_clock::from_time_t(candidateValue);
        return schedule.name + "|" + formatLocal(candidateValue, "%Y-%m-%dT%H:%M:%S%z");
    }
    return {};
}

void AutomationEngine::actionAccepted(const std::string& occurrenceKey) {
    if (occurrenceKey.empty() ||
        std::find(handledOccurrences_.begin(), handledOccurrences_.end(), occurrenceKey) !=
            handledOccurrences_.end()) return;
    handledOccurrences_.push_back(occurrenceKey);
    if (handledOccurrences_.size() > 64U) handledOccurrences_.erase(handledOccurrences_.begin());
    std::string error;
    if (!savePersistentState(&error)) appendLog("error", error);
}

void AutomationEngine::evaluateSchedules(std::chrono::system_clock::time_point now, bool force) {
    if (state_ == nullptr || !lastError_.empty() || manualOverride_ || !connected_ || schedules_.empty()) return;
    const Schedule* selected = nullptr;
    std::chrono::system_clock::time_point selectedTime{};
    std::string selectedKey;
    for (const Schedule& schedule : schedules_) {
        std::chrono::system_clock::time_point when;
        const std::string key = occurrenceFor(schedule, now, &when);
        if (key.empty()) continue;
        if (selected == nullptr || when > selectedTime) {
            selected = &schedule;
            selectedTime = when;
            selectedKey = key;
        }
    }
    if (selected == nullptr || (!force &&
        std::find(handledOccurrences_.begin(), handledOccurrences_.end(), selectedKey) !=
            handledOccurrences_.end())) return;
    lastAction_ = "Zeitplan " + selected->name + " fällig · " +
        formatLocal(std::chrono::system_clock::to_time_t(selectedTime), "%Y-%m-%dT%H:%M:%S%z");
    if (actionHandler_)
        actionHandler_({selected->values, "Lua-Zeitplan " + selected->name, selectedKey});
}

void AutomationEngine::processTime(std::chrono::system_clock::time_point now) {
    if (state_ == nullptr || manualOverride_) return;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    const std::string minute = formatLocal(value, "%Y-%m-%dT%H:%M");
    if (minute == lastMinute_) return;
    lastMinute_ = minute;
    callEvent("time", timeDetail(now));
    evaluateSchedules(now);
}

} // namespace airctrl
