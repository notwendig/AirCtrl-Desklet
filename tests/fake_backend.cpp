#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <cerrno>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopped=0;
void stop(int signal) { stopped=signal; }
struct ExitMarker {
    ~ExitMarker() {
        const auto path=qEnvironmentVariable("AIRCTRL_TEST_EXIT_FILE");
        if(stopped && !path.isEmpty()) { QFile marker(path); if(marker.open(QIODevice::WriteOnly)) marker.write(QByteArray::number(stopped)); }
    }
};
bool waitMs(int ms) {
    QElapsedTimer clock; clock.start();
    while(!stopped && clock.elapsed()<ms) QThread::msleep(5);
    return !stopped;
}
bool gate(const char* variable) {
    const auto path=qEnvironmentVariable(variable);
    while(!stopped && !path.isEmpty() && !QFileInfo::exists(path)) QThread::msleep(5);
    return !stopped;
}
bool gateOpen(const char* variable) {
    const auto path=qEnvironmentVariable(variable);
    return path.isEmpty() || QFileInfo::exists(path);
}
QJsonObject status() {
    QJsonObject object{{"name","Wohnzimmer"},{"modelid","AC2729/10"},{"pwr","1"},
        {"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"iaql",1},
        {"mode","P"},{"om","s"},{"func","PH"},{"cl",false},{"aqil",100},{"uil","1"},{"dt",0}};
    QFile state(qEnvironmentVariable("AIRCTRL_TEST_STATE"));
    if(state.open(QIODevice::ReadOnly)) {
        const auto saved=QJsonDocument::fromJson(state.readAll()).object();
        for(auto i=saved.begin();i!=saved.end();++i) object[i.key()]=i.value();
    }
    return object;
}
void appendLog(const QJsonArray& call) {
    QFile log(qEnvironmentVariable("AIRCTRL_TEST_LOG"));
    if(log.open(QIODevice::Append)) log.write(QJsonDocument(call).toJson(QJsonDocument::Compact)+'\n');
}
void output(const QJsonObject& state) {
    std::cout<<QJsonDocument(state).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
void outputSessionStatus(const QJsonObject& state) {
    output({{"_airctrl","status"},{"data",state}});
}
void outputControl(quint64 id,bool ok,const QString& error={}) {
    QJsonObject envelope{{"_airctrl","control"},{"id",static_cast<qint64>(id)},{"ok",ok}};
    if(!error.isEmpty()) envelope["error"]=error;
    output(envelope);
}
void logControl(const QJsonObject& values) {
    QJsonArray call{"set"};
    const bool integers=!values.isEmpty() && values.begin().value().isDouble();
    if(integers) call.append("-I");
    for(auto i=values.begin();i!=values.end();++i) {
        const auto value=i.value();
        const auto encoded=value.isBool() ? (value.toBool() ? "true" : "false") :
            value.isDouble() ? QString::number(value.toInt()) : value.toString();
        call.append(i.key()+"="+encoded);
    }
    appendLog(call);
}
bool saveValues(const QJsonObject& values) {
    auto object=status();
    for(auto i=values.begin();i!=values.end();++i) object[i.key()]=i.value();
    QSaveFile state(qEnvironmentVariable("AIRCTRL_TEST_STATE"));
    if(!state.open(QIODevice::WriteOnly)) return false;
    state.write(QJsonDocument(object).toJson());
    return state.commit();
}
struct Command { quint64 id=0; QJsonObject values; };

int session(const QString& mode) {
    const int flags=::fcntl(STDIN_FILENO,F_GETFL,0);
    if(flags<0 || ::fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)<0) return 1;
    QByteArray input;
    std::deque<Command> commands;
    std::optional<Command> pending;
    bool firstStatus=false;
    QElapsedTimer tick; tick.start();
    QElapsedTimer lifetime; lifetime.start();
    const int tickMs=qEnvironmentVariableIntValue("AIRCTRL_TEST_TICK_MS");

    const auto pump=[&]() -> bool {
        char buffer[4096];
        for(;;) {
            const auto count=::read(STDIN_FILENO,buffer,sizeof(buffer));
            if(count>0) { input.append(buffer,count); continue; }
            if(count==0) return false;
            if(errno==EINTR) continue;
            if(errno==EAGAIN || errno==EWOULDBLOCK) break;
            return false;
        }
        for(;;) {
            const auto newline=input.indexOf('\n');
            if(newline<0) break;
            const auto line=input.left(newline).trimmed(); input.remove(0,newline+1);
            const auto object=QJsonDocument::fromJson(line).object();
            if(!object.isEmpty() && object.value("values").isObject())
                commands.push_back({object.value("id").toVariant().toULongLong(),object.value("values").toObject()});
        }
        return true;
    };
    const auto emitInitial=[&] {
        if(firstStatus || !gateOpen("AIRCTRL_TEST_READ_GATE")) return;
        if(mode=="timeout") return;
        firstStatus=true; tick.restart();
        if(mode=="bad-json") std::cout<<"not JSON"<<std::endl;
        else if(mode=="oversized") std::cout<<std::string(1024*1024+1,'x')<<std::flush;
        else if(mode=="partial") {
            const auto line=QJsonDocument(QJsonObject{{"_airctrl","status"},{"data",status()}}).toJson(QJsonDocument::Compact);
            std::cout.write(line.constData(),line.size()/2); std::cout.flush(); waitMs(100);
            std::cout.write(line.constData()+line.size()/2,line.size()-line.size()/2); std::cout<<std::endl;
        } else if(mode=="batch") {
            outputSessionStatus({{"rh",41}}); outputSessionStatus({{"rh",42}}); outputSessionStatus({{"rh",43}});
        } else outputSessionStatus(status());
    };

    while(!stopped) {
        if(!pump()) return 0;
        emitInitial();
        if((mode=="bad-json" || mode=="oversized") && firstStatus) { waitMs(100); continue; }
        if(mode=="exit-stream" && firstStatus && lifetime.elapsed()>=100) return 0;
        if(!pending && !commands.empty()) { pending=commands.front(); commands.pop_front(); logControl(pending->values); }
        if(pending) {
            if(mode=="write-timeout") { waitMs(5); continue; }
            if(!gateOpen("AIRCTRL_TEST_WRITE_GATE")) { waitMs(5); continue; }
            const auto command=*pending; pending.reset();
            if(mode=="write-failure") outputControl(command.id,false,"Error: CoAP response timed out");
            else {
                if(!saveValues(command.values)) outputControl(command.id,false,"Could not save fake device state");
                else outputControl(command.id,true);
            }
            if(mode!="idle" && gateOpen("AIRCTRL_TEST_READ_GATE")) { outputSessionStatus(status()); tick.restart(); }
            continue;
        }
        if(!firstStatus || mode=="timeout" || mode=="idle" || mode=="partial" || mode=="batch") {
            waitMs(5); continue;
        }
        if(tick.elapsed()>=(tickMs>0 ? tickMs : 100)) {
            const auto notifyGate=qEnvironmentVariable("AIRCTRL_TEST_NOTIFY_GATE");
            if(notifyGate.isEmpty() || QFileInfo::exists(notifyGate)) outputSessionStatus(status());
            tick.restart();
        }
        waitMs(5);
    }
    return 0;
}
}

int main(int argc,char** argv) {
    ExitMarker marker;
    std::signal(SIGTERM,stop); std::signal(SIGINT,stop);
    QCoreApplication app(argc,argv);
    auto args=app.arguments(); args.removeFirst();
    appendLog(QJsonArray::fromStringList(args));
    const auto mode=qEnvironmentVariable("AIRCTRL_TEST_MODE");
    if(mode=="failure-once") {
        QFile records(qEnvironmentVariable("AIRCTRL_TEST_LOG")); records.open(QIODevice::ReadOnly);
        if(records.readAll().count('\n')==1) { std::cerr<<"Error: CoAP response timed out\n"; return 1; }
    }
    if(mode=="failure") { std::cerr<<"device offline\n"; return 1; }
    if(args.contains("session")) return session(mode);
    if(args.contains("set")) {
        if(!gate("AIRCTRL_TEST_WRITE_GATE")) return 0;
        if(mode=="write-timeout") { while(waitMs(100)) {} return 0; }
        if(mode=="write-failure") { std::cerr<<"Error: CoAP response timed out\n"; return 1; }
        QJsonObject values;
        for(const auto& arg:args) {
            const auto split=arg.indexOf('='); if(split<1) continue;
            const auto key=arg.left(split), value=arg.mid(split+1);
            if(args.contains("-I")) values[key]=value.toInt();
            else if(value=="true" || value=="false") values[key]=value=="true";
            else values[key]=value;
        }
        return saveValues(values) ? 0 : 1;
    }
    if(mode=="timeout") { while(waitMs(100)) {} return 0; }
    if(!gate("AIRCTRL_TEST_READ_GATE")) return 0;
    if(mode=="bad-json") { std::cout<<"not JSON"<<std::endl; while(waitMs(100)) {} return 0; }
    if(mode=="oversized") { std::cout<<std::string(1024*1024+1,'x')<<std::flush; while(waitMs(100)) {} return 0; }
    if(mode=="partial") {
        const auto line=QJsonDocument(status()).toJson(QJsonDocument::Compact);
        std::cout.write(line.constData(),line.size()/2); std::cout.flush();
        if(!waitMs(100)) return 0;
        std::cout.write(line.constData()+line.size()/2,line.size()-line.size()/2); std::cout<<std::endl;
    } else if(mode=="batch") {
        std::cout<<"{\"rh\":41}\n{\"rh\":42}\n{\"rh\":43}\n"<<std::flush;
    } else output(status());
    if(!args.contains("status-observe")) return 0;
    if(mode=="exit-stream") { waitMs(100); return 0; }
    if(mode=="idle" || mode=="partial" || mode=="batch") { while(waitMs(100)) {} return 0; }
    const int tick=qEnvironmentVariableIntValue("AIRCTRL_TEST_TICK_MS");
    while(waitMs(tick>0 ? tick : 100)) {
        const auto notifyGate=qEnvironmentVariable("AIRCTRL_TEST_NOTIFY_GATE");
        if(!notifyGate.isEmpty() && !QFileInfo::exists(notifyGate)) continue;
        output(status());
    }
    return 0;
}
