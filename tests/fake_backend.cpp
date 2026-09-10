#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
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

QString currentMode() {
    QFile file(qEnvironmentVariable("AIRCTRL_TEST_MODE_FILE"));
    if(file.open(QIODevice::ReadOnly)) return QString::fromUtf8(file.readAll()).trimmed();
    return qEnvironmentVariable("AIRCTRL_TEST_MODE");
}

struct IpcCommand {
    QPointer<QTcpSocket> socket;
    quint64 id = 0;
    QJsonObject values;
};

class FakeServer final : public QObject {
    Q_OBJECT
public:
    explicit FakeServer(quint16 port,QObject* parent=nullptr):QObject(parent) {
        requestMs_=qEnvironmentVariableIntValue("AIRCTRL_TEST_REQUEST_MS");
        idleMs_=qEnvironmentVariableIntValue("AIRCTRL_TEST_IDLE_MS");
        reconnectMs_=qEnvironmentVariableIntValue("AIRCTRL_TEST_DEVICE_RECONNECT_MS");
        if(requestMs_<=0) requestMs_=60000;
        if(idleMs_<=0) idleMs_=90000;
        if(reconnectMs_<=0) reconnectMs_=10000;
        if(!server_.listen(QHostAddress::LocalHost,port)) throw std::runtime_error(server_.errorString().toStdString());
        connect(&server_,&QTcpServer::newConnection,this,&FakeServer::accept);
        tick_.setInterval(5); connect(&tick_,&QTimer::timeout,this,&FakeServer::step); tick_.start();
        beginAttempt();
        if(qEnvironmentVariableIntValue("AIRCTRL_SERVER_EXIT_ON_IDLE")==1)
            QTimer::singleShot(5000,this,[this] { if(clients_.isEmpty()) QCoreApplication::quit(); });
    }
    ~FakeServer() override { server_.close(); }
private:
    void accept() {
        while(auto* socket=server_.nextPendingConnection()) {
            clients_.append(socket); buffers_[socket]={};
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{ read(socket); });
            connect(socket,&QTcpSocket::disconnected,this,[this,socket]{
                clients_.removeAll(socket); buffers_.remove(socket); socket->deleteLater();
                if(pending_ && pending_->socket==socket) pending_.reset();
                if(clients_.isEmpty()) QTimer::singleShot(0,this,[this]{
                    if(clients_.isEmpty()) QCoreApplication::quit();
                });
            });
            send(socket,{{"_airctrl","state"},{"state",firstStatus_ ? "connected" : "connecting"},{"starts",starts_}});
            if(firstStatus_ && !lastStatus_.isEmpty())
                send(socket,{{"_airctrl","status"},{"data",lastStatus_}});
        }
    }
    void read(QTcpSocket* socket) {
        auto& buffer=buffers_[socket]; buffer+=socket->readAll();
        for(;;) {
            const auto newline=buffer.indexOf('\n'); if(newline<0) break;
            const auto line=buffer.left(newline).trimmed(); buffer.remove(0,newline+1);
            const auto object=QJsonDocument::fromJson(line).object(); if(object.isEmpty()) continue;
            const auto kind=object.value("_airctrl").toString();
            if(kind=="configure") send(socket,{{"_airctrl","error"},{"error","device settings are server-only"}});
            else if(kind=="refresh") scheduleRefresh();
            else if(kind=="control") control(socket,object);
            else if(kind=="ping") send(socket,{{"_airctrl","pong"}});
        }
    }
    void beginAttempt() {
        refreshScheduled_=false;
        ++starts_; firstStatus_=false; attempt_.restart(); statusTick_.restart();
        QJsonArray call{"-H",host_,"-P",QString::number(port_),"-D","--timeout",
            QString::number((requestMs_+999)/1000),"--control-timeout","10","--idle-timeout",
            QString::number((idleMs_+999)/1000),"session","-J"};
        appendLog(call); broadcast({{"_airctrl","state"},{"state","connecting"},{"starts",starts_}});
    }
    void scheduleRefresh() {
        // Several clients (or repeated F5 key events) can request a refresh in
        // one event-loop turn. They all mean one replacement I/O session.
        if(refreshScheduled_) return;
        refreshScheduled_=true;
        QTimer::singleShot(0,this,[this] { beginAttempt(); });
    }
    void step() {
        if(retryPending_ || clients_.isEmpty()) return;
        const auto mode=currentMode();
        if(!firstStatus_) {
            if(mode=="failure" || mode=="failure-once") {
                if(mode=="failure-once" && starts_>1) { emitStatus(status()); return; }
                if(attempt_.elapsed()>=20) failAttempt("device offline");
                return;
            }
            if(mode=="timeout") { if(attempt_.elapsed()>=requestMs_) failAttempt("CoAP response timed out"); return; }
            if(mode=="bad-json" || mode=="oversized") { if(attempt_.elapsed()>=20) failAttempt("Ungültige Gerätestatusmeldung"); return; }
            if(!gateOpen("AIRCTRL_TEST_READ_GATE")) return;
            if(mode=="batch") {
                emitStatus({{"rh",41}}); emitStatus({{"rh",42}}); emitStatus({{"rh",43}}); return;
            }
            emitStatus(status());
            return;
        }
        if(mode=="exit-stream" && attempt_.elapsed()>=100) { failAttempt("I/O-Sitzung wurde unerwartet beendet."); return; }
        if(pending_) {
            if(!pending_->socket) pending_.reset();
            else if(mode!="write-timeout" && gateOpen("AIRCTRL_TEST_WRITE_GATE")) {
                const auto command=*pending_; pending_.reset(); finishControl(command,mode);
            }
            // A real I/O session pauses Observe while a device control is
            // pending. The fake server must not leak status packets here.
            if(pending_) return;
        }
        if(mode=="idle" || mode=="partial" || mode=="batch") {
            if(mode=="idle" && statusTick_.elapsed()>=idleMs_) failAttempt("Observation idle timeout");
            return;
        }
        const int interval=qEnvironmentVariableIntValue("AIRCTRL_TEST_TICK_MS");
        if(statusTick_.elapsed()>=(interval>0?interval:100)) {
            const auto gate=qEnvironmentVariable("AIRCTRL_TEST_NOTIFY_GATE");
            if(gate.isEmpty() || QFileInfo::exists(gate)) emitStatus(status());
            else statusTick_.restart();
        }
    }
    void failAttempt(const QString& error) {
        // The outcome of a command interrupted by a new I/O session is
        // unknown. Never retain or replay it in the replacement session.
        pending_.reset();
        firstStatus_=false;
        retryPending_=true;
        broadcast({{"_airctrl","state"},{"state","error"},{"starts",starts_},{"error",error}});
        QTimer::singleShot(qMax(20,reconnectMs_),this,[this]{ retryPending_=false; beginAttempt(); });
    }
    void control(QTcpSocket* socket,const QJsonObject& object) {
        const auto id=object.value("id").toVariant().toULongLong();
        const auto values=object.value("values").toObject(); logControl(values);
        const auto mode=currentMode();
        IpcCommand command{socket,id,values};
        if(mode=="write-timeout" || !gateOpen("AIRCTRL_TEST_WRITE_GATE")) {
            pending_=command; return;
        }
        finishControl(command,mode);
    }
    void finishControl(const IpcCommand& command,const QString& mode) {
        auto* socket=command.socket.data(); if(!socket) return;
        if(mode=="write-failure") { send(socket,{{"_airctrl","control"},{"id",static_cast<qint64>(command.id)},
            {"ok",false},{"error","Error: CoAP response timed out"}}); return; }
        if(!saveValues(command.values)) { send(socket,{{"_airctrl","control"},{"id",static_cast<qint64>(command.id)},
            {"ok",false},{"error","Could not save fake device state"}}); return; }
        send(socket,{{"_airctrl","control"},{"id",static_cast<qint64>(command.id)},{"ok",true}});
        if(mode!="idle" && gateOpen("AIRCTRL_TEST_READ_GATE")) emitStatus(status());
    }
    void emitStatus(const QJsonObject& value) {
        firstStatus_=true; lastStatus_=value; statusTick_.restart();
        broadcast({{"_airctrl","status"},{"data",value}});
    }
    void send(QTcpSocket* socket,const QJsonObject& object) {
        if(socket->state()==QAbstractSocket::ConnectedState)
            socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');
    }
    void broadcast(const QJsonObject& object) { for(auto* socket:clients_) send(socket,object); }

    QString host_="AC2729-10"; int port_=5683,requestMs_=60000,idleMs_=90000,reconnectMs_=10000;
    QTcpServer server_; QList<QTcpSocket*> clients_; QHash<QTcpSocket*,QByteArray> buffers_;
    std::optional<IpcCommand> pending_;
    QTimer tick_; QElapsedTimer attempt_,statusTick_; qint64 starts_=0;
    QJsonObject lastStatus_;
    bool firstStatus_=false,refreshScheduled_=false,retryPending_=false;
};

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
    bool portOk=false;
    const auto serverPort=qEnvironmentVariable("AIRCTRL_TEST_SERVER_PORT").toUInt(&portOk);
    if(portOk && serverPort>0 && serverPort<=65535) {
        try { FakeServer server(static_cast<quint16>(serverPort)); return app.exec(); }
        catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    }
    appendLog(QJsonArray::fromStringList(args));
    const auto mode=qEnvironmentVariable("AIRCTRL_TEST_MODE");
    if(mode=="failure-once") {
        QFile records(qEnvironmentVariable("AIRCTRL_TEST_LOG"));
        if (!records.open(QIODevice::ReadOnly)) {
            std::cerr << "Cannot read fake-backend log\n";
            return 1;
        }
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

#include "fake_backend.moc"
