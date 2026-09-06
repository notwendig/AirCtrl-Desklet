#include "automation.hpp"
#include "airctrl_automation_example.hpp"
#include "controlvalues.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace {
char engineRegistryKey;

void pushJson(lua_State* state, const QJsonValue& value) {
    if(value.isNull() || value.isUndefined()) lua_pushnil(state);
    else if(value.isBool()) lua_pushboolean(state,value.toBool());
    else if(value.isDouble()) {
        const auto number=value.toDouble();
        if(number==qint64(number)) lua_pushinteger(state,static_cast<lua_Integer>(number));
        else lua_pushnumber(state,number);
    } else if(value.isString()) {
        const auto utf8=value.toString().toUtf8(); lua_pushlstring(state,utf8.constData(),size_t(utf8.size()));
    } else if(value.isArray()) {
        const auto array=value.toArray(); lua_createtable(state,array.size(),0);
        for(int i=0;i<array.size();++i) { pushJson(state,array[i]); lua_rawseti(state,-2,i+1); }
    } else {
        const auto object=value.toObject(); lua_createtable(state,0,object.size());
        for(auto i=object.begin();i!=object.end();++i) {
            const auto key=i.key().toUtf8(); pushJson(state,i.value()); lua_setfield(state,-2,key.constData());
        }
    }
}

QString luaString(lua_State* state, int index) {
    size_t length=0; const auto* text=lua_tolstring(state,index,&length);
    return text ? QString::fromUtf8(text,qsizetype(length)) : QString();
}

QJsonObject simpleTable(lua_State* state, int index, QString* error) {
    QJsonObject result;
    const int absolute=lua_absindex(state,index);
    if(!lua_istable(state,absolute)) { *error="Tabelle mit Steuerwerten erwartet."; return {}; }
    lua_pushnil(state);
    while(lua_next(state,absolute)!=0) {
        if(lua_type(state,-2)!=LUA_TSTRING) { *error="Steuerfelder müssen Textschlüssel haben."; lua_pop(state,2); return {}; }
        const auto key=luaString(state,-2);
        switch(lua_type(state,-1)) {
        case LUA_TBOOLEAN: result.insert(key,bool(lua_toboolean(state,-1))); break;
        case LUA_TSTRING: result.insert(key,luaString(state,-1)); break;
        case LUA_TNUMBER:
            if(!lua_isinteger(state,-1)) { *error="Steuerzahlen müssen ganzzahlig sein: "+key; lua_pop(state,2); return {}; }
            result.insert(key,qint64(lua_tointeger(state,-1))); break;
        default: *error="Unzulässiger Lua-Wert für Steuerfeld: "+key; lua_pop(state,2); return {};
        }
        lua_pop(state,1);
    }
    *error=controlValuesError(result);
    return error->isEmpty() ? result : QJsonObject{};
}

QJsonObject timeDetail(const QDateTime& now) {
    return {{"iso",now.toString(Qt::ISODate)},
        {"date",now.date().toString(Qt::ISODate)},
        {"time",now.time().toString("HH:mm")},
        {"year",now.date().year()},{"month",now.date().month()},{"day",now.date().day()},
        {"weekday",now.date().dayOfWeek()},{"hour",now.time().hour()},{"minute",now.time().minute()}};
}
}

AutomationEngine::AutomationEngine(bool persistent, QObject* parent)
    : QObject(parent), persistent_(persistent) {
    if(persistent_) handledOccurrences_=QSettings().value("automation/handledOccurrences").toStringList();
}

AutomationEngine::~AutomationEngine() { closeState(); }

QString AutomationEngine::scriptPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/automation.lua";
}

QString AutomationEngine::luaRelease() { return QString::fromLatin1(LUA_RELEASE); }

QString AutomationEngine::exampleScript() {
    return QString::fromUtf8(AirctrlAutomationExample);
}

void AutomationEngine::setEnabled(bool enabled) {
    if(enabled_==enabled && (!enabled || state_)) return;
    enabled_=enabled;
    if(persistent_) { QSettings settings; settings.setValue("automation/enabled",enabled_); }
    if(enabled_) reload();
    else {
        closeState(); lastError_.clear(); lastEvent_="deaktiviert"; emit problemChanged({});
    }
}

bool AutomationEngine::reload() {
    if(!enabled_) return false;
    QFile file(scriptPath());
    if(!file.exists()) {
        QString error;
        if(!saveScript(exampleScript(),&error)) { setProblem(error); return false; }
    }
    if(!file.open(QIODevice::ReadOnly)) { setProblem("Lua-Skript kann nicht gelesen werden: "+file.errorString()); return false; }
    if(file.size()>256*1024) { setProblem("Lua-Skript ist größer als 256 KiB."); return false; }
    return loadScriptText(QString::fromUtf8(file.readAll()),scriptPath());
}

bool AutomationEngine::saveScript(const QString& text, QString* error) {
    if(text.toUtf8().size()>256*1024) {
        if(error) *error="Lua-Skript ist größer als 256 KiB.";
        return false;
    }
    const QFileInfo info(scriptPath());
    if(!QDir().mkpath(info.absolutePath())) {
        if(error) *error="Konfigurationsordner konnte nicht angelegt werden.";
        return false;
    }
    QSaveFile file(scriptPath());
    if(!file.open(QIODevice::WriteOnly)) { if(error) *error=file.errorString(); return false; }
    const auto bytes=text.toUtf8();
    if(file.write(bytes)!=bytes.size() || !file.commit()) { if(error) *error=file.errorString(); return false; }
    if(error) error->clear();
    return true;
}

void* AutomationEngine::allocator(void* userData, void* pointer, size_t oldSize, size_t newSize) {
    auto* memory=static_cast<MemoryLimit*>(userData);
    if(newSize==0) {
        std::free(pointer); memory->used=oldSize>memory->used ? 0 : memory->used-oldSize; return nullptr;
    }
    const size_t base=pointer ? (oldSize>memory->used ? 0 : memory->used-oldSize) : memory->used;
    if(newSize>memory->maximum || base>memory->maximum-newSize) return nullptr;
    void* result=std::realloc(pointer,newSize);
    if(result) memory->used=base+newSize;
    return result;
}

void AutomationEngine::instructionHook(lua_State* state, lua_Debug*) {
    luaL_error(state,"Ausführungslimit der Lua-Automatik überschritten");
}

AutomationEngine* AutomationEngine::fromLua(lua_State* state) {
    lua_pushlightuserdata(state,&engineRegistryKey); lua_gettable(state,LUA_REGISTRYINDEX);
    auto* result=static_cast<AutomationEngine*>(lua_touserdata(state,-1)); lua_pop(state,1); return result;
}

void AutomationEngine::closeState() {
    pendingActions_.clear(); schedules_.clear();
    if(state_) { lua_close(state_); state_=nullptr; }
    memory_={};
}

void AutomationEngine::openSandbox() {
    const luaL_Reg libraries[]={{LUA_GNAME,luaopen_base},{LUA_TABLIBNAME,luaopen_table},
        {LUA_STRLIBNAME,luaopen_string},{LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8},{nullptr,nullptr}};
    for(const auto* library=libraries;library->func;++library) {
        luaL_requiref(state_,library->name,library->func,1); lua_pop(state_,1);
    }
    for(const auto* name : {"dofile","loadfile","load"}) { lua_pushnil(state_); lua_setglobal(state_,name); }
}

void AutomationEngine::registerApi() {
    lua_pushlightuserdata(state_,&engineRegistryKey); lua_pushlightuserdata(state_,this);
    lua_settable(state_,LUA_REGISTRYINDEX);
    lua_createtable(state_,0,6);
    lua_pushcfunction(state_,&AutomationEngine::luaLog); lua_setfield(state_,-2,"log");
    lua_pushcfunction(state_,&AutomationEngine::luaSet); lua_setfield(state_,-2,"set");
    lua_pushcfunction(state_,&AutomationEngine::luaSchedule); lua_setfield(state_,-2,"schedule");
    lua_pushcfunction(state_,&AutomationEngine::luaStatus); lua_setfield(state_,-2,"status");
    const auto version=QCoreApplication::applicationVersion().toUtf8();
    lua_pushlstring(state_,version.constData(),size_t(version.size())); lua_setfield(state_,-2,"version");
    lua_pushstring(state_,LUA_RELEASE); lua_setfield(state_,-2,"lua_version");
    lua_setglobal(state_,"airctrl");
    lua_pushcfunction(state_,&AutomationEngine::luaLog); lua_setglobal(state_,"print");
}

bool AutomationEngine::loadScriptText(const QString& text, const QString& sourceName) {
    closeState(); lastError_.clear(); lastEvent_.clear(); lastAction_.clear(); lastMinute_.clear(); lastAlertDigest_.clear();
    memory_={}; state_=lua_newstate(&AutomationEngine::allocator,&memory_);
    if(!state_) { setProblem("Lua konnte wegen des Speicherlimits nicht gestartet werden."); return false; }
    openSandbox(); registerApi();
    const auto bytes=text.toUtf8(); const auto source=sourceName.toUtf8();
    int result=luaL_loadbufferx(state_,bytes.constData(),size_t(bytes.size()),source.constData(),"t");
    if(result==LUA_OK) {
        lua_sethook(state_,&AutomationEngine::instructionHook,LUA_MASKCOUNT,instructionBudget_);
        dispatching_=true; result=lua_pcall(state_,0,0,0); dispatching_=false; lua_sethook(state_,nullptr,0,0);
    }
    if(result!=LUA_OK) {
        const auto problem="Lua-Skriptfehler: "+luaString(state_,-1); lua_pop(state_,1); closeState(); setProblem(problem); return false;
    }
    lastEvent_="Skript geladen"; emit problemChanged({});
    callEvent("startup",{{"time",timeDetail(QDateTime::currentDateTime())}});
    processTime();
    return loaded();
}

void AutomationEngine::setProblem(const QString& problem) {
    lastError_=problem; appendLog("error",problem); emit problemChanged(problem);
}

void AutomationEngine::appendLog(const QString& level, const QString& message) {
    const auto clean=message.left(1000).replace('\n',' ');
    const auto entry=QDateTime::currentDateTime().toString(Qt::ISODate)+" ["+level+"] "+clean;
    logEntries_.append(entry); while(logEntries_.size()>50) logEntries_.removeFirst();
    emit logMessage(entry);
}

int AutomationEngine::luaLog(lua_State* state) {
    auto* self=fromLua(state); if(!self) return 0;
    QString level="info", message;
    if(lua_gettop(state)>=2) { level=luaL_checkstring(state,1); message=luaL_checkstring(state,2); }
    else message=luaL_checkstring(state,1);
    if(!QStringList{"debug","info","warning","error"}.contains(level)) level="info";
    self->appendLog(level,message); return 0;
}

int AutomationEngine::luaSet(lua_State* state) {
    auto* self=fromLua(state); if(!self) return 0;
    if(!QStringList{"connected","status","alarm","time"}.contains(self->currentEvent_))
        return luaL_error(state,"airctrl.set ist nur in connected-, status-, alarm- oder time-Ereignissen erlaubt");
    if(!self->pendingActions_.isEmpty()) return luaL_error(state,"pro Ereignis ist nur ein airctrl.set-Auftrag erlaubt");
    QString error; const auto values=simpleTable(state,1,&error);
    if(!error.isEmpty()) return luaL_error(state,"%s",error.toUtf8().constData());
    self->pendingActions_.append({values,"Lua-Ereignis "+self->currentEvent_,{}}); return 0;
}

int AutomationEngine::luaSchedule(lua_State* state) {
    auto* self=fromLua(state); if(!self) return 0;
    luaL_checktype(state,1,LUA_TTABLE);
    if(self->schedules_.size()>=64) return luaL_error(state,"höchstens 64 Zeitpläne erlaubt");
    const int table=lua_absindex(state,1);
    auto fieldString=[&](const char* name) { lua_getfield(state,table,name); const auto result=luaString(state,-1); lua_pop(state,1); return result; };
    Schedule schedule; schedule.name=fieldString("name"); const auto at=fieldString("at");
    static const QRegularExpression validName("^[A-Za-z0-9_.-]{1,64}$");
    if(!validName.match(schedule.name).hasMatch()) return luaL_error(state,"schedule.name: 1-64 Zeichen aus A-Z, a-z, 0-9, _.- erwartet");
    schedule.at=QTime::fromString(at,"HH:mm");
    if(!schedule.at.isValid() || schedule.at.toString("HH:mm")!=at) return luaL_error(state,"schedule.at: HH:MM erwartet");
    for(const auto& existing:self->schedules_) if(existing.name==schedule.name)
        return luaL_error(state,"doppelter Zeitplanname: %s",schedule.name.toUtf8().constData());
    lua_getfield(state,table,"days");
    if(lua_isnil(state,-1)) for(int day=1;day<=7;++day) schedule.days.insert(day);
    else {
        if(!lua_istable(state,-1)) { lua_pop(state,1); return luaL_error(state,"schedule.days: Tabelle erwartet"); }
        const auto count=lua_rawlen(state,-1);
        for(size_t i=1;i<=count;++i) { lua_rawgeti(state,-1,lua_Integer(i));
            const int day=int(luaL_checkinteger(state,-1)); lua_pop(state,1);
            if(day<1 || day>7) { lua_pop(state,1); return luaL_error(state,"schedule.days: Wochentage 1 bis 7 erwartet"); }
            schedule.days.insert(day);
        }
        if(schedule.days.isEmpty()) { lua_pop(state,1); return luaL_error(state,"schedule.days darf nicht leer sein"); }
    }
    lua_pop(state,1);
    lua_getfield(state,table,"catch_up");
    if(!lua_isnil(state,-1) && !lua_isboolean(state,-1)) {
        lua_pop(state,1); return luaL_error(state,"schedule.catch_up: Boolean erwartet");
    }
    if(!lua_isnil(state,-1)) schedule.catchUp=lua_toboolean(state,-1);
    lua_pop(state,1);
    lua_getfield(state,table,"set"); QString error; schedule.values=simpleTable(state,-1,&error); lua_pop(state,1);
    if(!error.isEmpty()) return luaL_error(state,"schedule.set: %s",error.toUtf8().constData());
    for(const auto& existing:self->schedules_) {
        auto overlap=existing.days; overlap.intersect(schedule.days);
        if(existing.at==schedule.at && !overlap.isEmpty())
            return luaL_error(state,"Zeitpläne %s und %s überschneiden sich zur selben Uhrzeit",
                existing.name.toUtf8().constData(),schedule.name.toUtf8().constData());
    }
    self->schedules_.append(schedule); return 0;
}

int AutomationEngine::luaStatus(lua_State* state) {
    auto* self=fromLua(state); if(!self) { lua_pushnil(state); return 1; }
    pushJson(state,self->latestStatus_); return 1;
}

bool AutomationEngine::callEvent(const QString& type, const QJsonObject& detail) {
    if(!state_ || dispatching_ || !lastError_.isEmpty()) return false;
    lua_getglobal(state_,"on_event");
    if(lua_isnil(state_,-1)) { lua_pop(state_,1); return true; }
    if(!lua_isfunction(state_,-1)) { lua_pop(state_,1); setProblem("Lua: on_event ist keine Funktion."); return false; }
    QJsonObject event=detail; event.insert("type",type); event.insert("timestamp",QDateTime::currentDateTime().toString(Qt::ISODate));
    pushJson(state_,event); currentEvent_=type; dispatching_=true;
    lua_sethook(state_,&AutomationEngine::instructionHook,LUA_MASKCOUNT,instructionBudget_);
    const int result=lua_pcall(state_,1,0,0);
    lua_sethook(state_,nullptr,0,0); dispatching_=false; currentEvent_.clear();
    if(result!=LUA_OK) {
        const auto problem="Lua-Ereignis "+type+": "+luaString(state_,-1); lua_pop(state_,1);
        pendingActions_.clear(); setProblem(problem); return false;
    }
    lastEvent_=type+" · "+QDateTime::currentDateTime().toString(Qt::ISODate);
    flushActions(); return true;
}

void AutomationEngine::flushActions() {
    const auto actions=pendingActions_; pendingActions_.clear();
    for(const auto& action:actions) emit actionRequested(action.values,action.source,action.occurrenceKey);
}

void AutomationEngine::setConnected(bool connected, const QString& reason, const QDateTime& now) {
    if(connected_==connected) return;
    connected_=connected;
    callEvent(connected ? "connected" : "disconnected",reason.isEmpty() ? QJsonObject{} : QJsonObject{{"reason",reason}});
    if(connected_) evaluateSchedules(now);
}

void AutomationEngine::statusEvent(const QJsonObject& status, const QDateTime& now) {
    QJsonObject changed;
    for(auto i=status.begin();i!=status.end();++i) if(!latestStatus_.contains(i.key()) || latestStatus_.value(i.key())!=i.value())
        changed.insert(i.key(),QJsonObject{{"old",latestStatus_.value(i.key())},{"new",i.value()}});
    for(auto i=latestStatus_.begin();i!=latestStatus_.end();++i) if(!status.contains(i.key()))
        changed.insert(i.key(),QJsonObject{{"old",i.value()},{"new",QJsonValue::Null}});
    const bool first=latestStatus_.isEmpty(); latestStatus_=status;
    if(!connected_) setConnected(true,{},now);
    callEvent("status",{{"status",status},{"changed",changed},{"first",first}});
    evaluateSchedules(now);
}

void AutomationEngine::alertsEvent(const QList<Alert>& alerts) {
    QJsonArray list; QStringList digestParts; QString highest="none";
    for(const auto& alert:alerts) {
        const auto level=alert.level==AlertLevel::Error ? QString("error") : QString("warning");
        if(alert.level==AlertLevel::Error) highest="error"; else if(highest=="none") highest="warning";
        list.append(QJsonObject{{"id",alert.key},{"level",level},{"message",alert.message}});
        digestParts.append(alert.key+":"+level);
    }
    std::sort(digestParts.begin(),digestParts.end()); const auto digest=digestParts.join('|');
    if(digest==lastAlertDigest_) return;
    lastAlertDigest_=digest;
    callEvent("alarm",{{"alerts",list},{"count",alerts.size()},{"highest",highest}});
}

void AutomationEngine::commandEvent(const QString& source, bool ok, const QString& message) {
    lastAction_=source+" · "+(ok ? "bestätigt" : "fehlgeschlagen")+" · "+QDateTime::currentDateTime().toString(Qt::ISODate);
    callEvent("command",{{"source",source},{"ok",ok},{"message",message}});
}

QString AutomationEngine::occurrenceFor(const Schedule& schedule, const QDateTime& now, QDateTime* when) const {
    for(int back=0;back<=7;++back) {
        const auto date=now.date().addDays(-back); if(!schedule.days.contains(date.dayOfWeek())) continue;
        const QDateTime candidate(date,schedule.at,now.timeZone());
        if(!candidate.isValid() || candidate>now) continue;
        if(!schedule.catchUp && candidate.date()!=now.date()) return {};
        if(!schedule.catchUp && candidate.time().hour()!=now.time().hour()) return {};
        if(!schedule.catchUp && candidate.time().minute()!=now.time().minute()) return {};
        if(when) *when=candidate;
        return schedule.name+"|"+candidate.toString(Qt::ISODate);
    }
    return {};
}

bool AutomationEngine::occurrenceHandled(const QString& key) const { return handledOccurrences_.contains(key); }

void AutomationEngine::rememberOccurrence(const QString& key) {
    if(key.isEmpty() || handledOccurrences_.contains(key)) return;
    handledOccurrences_.append(key); while(handledOccurrences_.size()>64) handledOccurrences_.removeFirst();
    if(persistent_) QSettings().setValue("automation/handledOccurrences",handledOccurrences_);
}

void AutomationEngine::actionAccepted(const QString& occurrenceKey) { rememberOccurrence(occurrenceKey); }

void AutomationEngine::evaluateSchedules(const QDateTime& now) {
    if(!state_ || !lastError_.isEmpty() || !connected_ || schedules_.isEmpty()) return;
    const Schedule* selected=nullptr; QDateTime selectedTime; QString selectedKey;
    for(const auto& schedule:schedules_) {
        QDateTime when; const auto key=occurrenceFor(schedule,now,&when);
        if(key.isEmpty()) continue;
        if(!selected || when>selectedTime) { selected=&schedule; selectedTime=when; selectedKey=key; }
    }
    if(!selected || occurrenceHandled(selectedKey)) return;
    lastAction_="Zeitplan "+selected->name+" fällig · "+selectedTime.toString(Qt::ISODate);
    emit actionRequested(selected->values,"Lua-Zeitplan "+selected->name,selectedKey);
}

void AutomationEngine::processTime(const QDateTime& now) {
    if(!state_ || !now.isValid()) return;
    const auto minute=now.toString("yyyy-MM-ddTHH:mm");
    if(minute==lastMinute_) return;
    lastMinute_=minute;
    callEvent("time",timeDetail(now)); evaluateSchedules(now);
}

QString AutomationEngine::diagnostics() const {
    const auto state=!enabled_ ? QString("deaktiviert") : loaded() ? QString("geladen") : QString("Fehler");
    QString text="Lua-Automatik = "+state+"\n"
        "Lua-Version = "+luaRelease()+" (eingebettet)\n"
        "Skript = "+scriptPath()+"\n"
        "Sandbox = ohne io, os, package, debug, dofile, loadfile und load; 8 MiB / 200000 Instruktionen je Aufruf\n"
        "Zeitpläne = "+QString::number(scheduleCount())+"\n"
        "Letztes Ereignis = "+(lastEvent_.isEmpty() ? QString("—") : lastEvent_)+"\n"
        "Letzte Aktion = "+(lastAction_.isEmpty() ? QString("—") : lastAction_)+"\n";
    if(!lastError_.isEmpty()) text+="Letzter Lua-Fehler = "+lastError_+"\n";
    if(!logEntries_.isEmpty()) text+="Lua-Protokoll:\n  "+logEntries_.join("\n  ")+"\n";
    return text;
}
