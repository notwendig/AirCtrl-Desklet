#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <csignal>
#include <iostream>

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
void output(const QJsonObject& state) {
    std::cout<<QJsonDocument(state).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
}
int main(int argc,char** argv) {
    ExitMarker marker;
    std::signal(SIGTERM,stop); std::signal(SIGINT,stop);
    QCoreApplication app(argc,argv);
    auto args=app.arguments(); args.removeFirst();
    QFile log(qEnvironmentVariable("AIRCTRL_TEST_LOG"));
    if(log.open(QIODevice::Append)) {
        log.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact)+'\n');
        log.close();
    }
    const auto mode=qEnvironmentVariable("AIRCTRL_TEST_MODE");
    if(mode=="failure-once") {
        QFile records(qEnvironmentVariable("AIRCTRL_TEST_LOG")); records.open(QIODevice::ReadOnly);
        if(records.readAll().count('\n')==1) { std::cerr<<"Error: CoAP response timed out\n"; return 1; }
    }
    if(mode=="failure") { std::cerr<<"device offline\n"; return 1; }
    if(args.contains("set")) {
        if(!gate("AIRCTRL_TEST_WRITE_GATE")) return 0;
        if(mode=="write-timeout") { while(waitMs(100)) {} return 0; }
        if(mode=="write-failure") { std::cerr<<"Error: CoAP response timed out\n"; return 1; }
        auto object=status();
        for(const auto& arg:args) {
            const auto split=arg.indexOf('='); if(split<1) continue;
            const auto key=arg.left(split), value=arg.mid(split+1);
            if(args.contains("-I")) object[key]=value.toInt();
            else if(value=="true" || value=="false") object[key]=value=="true";
            else object[key]=value;
        }
        QSaveFile state(qEnvironmentVariable("AIRCTRL_TEST_STATE"));
        if(!state.open(QIODevice::WriteOnly)) return 1;
        state.write(QJsonDocument(object).toJson());
        if(!state.commit()) return 1;
        return 0;
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
