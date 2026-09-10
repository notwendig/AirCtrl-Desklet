#include "controlvalues.hpp"
#include "ipc.hpp"
#include "airctrl_version.hpp"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <iostream>

namespace {
QJsonValue typedValue(const QString& key,const QString& text) {
    if(key=="cl") {
        if(text=="true" || text=="1") return true;
        if(text=="false" || text=="0") return false;
    }
    if(QStringList{"rhset","aqil","dt"}.contains(key)) {
        bool ok=false; const auto value=text.toInt(&ok); if(ok) return value;
    }
    return text;
}
void writeLine(QTcpSocket& socket,const QJsonObject& object) {
    socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');
    if(!socket.waitForBytesWritten(3000)) throw std::runtime_error(socket.errorString().toStdString());
}
QJsonObject nextLine(QTcpSocket& socket,QByteArray& buffer,int timeoutMs) {
    for(;;) {
        const auto newline=buffer.indexOf('\n');
        if(newline>=0) {
            const auto line=buffer.left(newline).trimmed(); buffer.remove(0,newline+1);
            const auto document=QJsonDocument::fromJson(line);
            if(!document.isObject()) throw std::runtime_error("Invalid server JSON");
            return document.object();
        }
        if(buffer.size()>1024*1024) throw std::runtime_error("Server message exceeds 1 MiB");
        if(!socket.waitForReadyRead(timeoutMs)) throw std::runtime_error(socket.errorString().toStdString());
        buffer+=socket.readAll();
    }
}
void print(const QJsonObject& object,bool pretty) {
    std::cout<<QJsonDocument(object).toJson(pretty?QJsonDocument::Indented:QJsonDocument::Compact).constData();
    if(!pretty) std::cout<<'\n';
    std::cout.flush();
}
} // namespace

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName("airctrl-client");
    QCoreApplication::setApplicationVersion(AIRCTRL_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("TCP-Client für den AirControl-Server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {{"H","host"},"AirControl-Server","host",defaultAirctrlServerHost()},
        {{"P","port"},"TCP-Port des AirControl-Servers","port",QString::number(defaultAirctrlServerPort())},
        {"timeout","Antwortfrist in Sekunden","seconds","120"},
        {{"J","json"},"Kompaktes JSON"},
    });
    parser.addPositionalArgument("command","status | watch | set | refresh | server-status");
    parser.addPositionalArgument("values","Für set: KEY=VALUE ...","[values...]");
    parser.process(app);
    const auto positional=parser.positionalArguments();
    if(positional.isEmpty()) parser.showHelp(2);
    const auto command=positional.first();
    if(!QStringList{"status","watch","set","refresh","server-status"}.contains(command)) {
        std::cerr<<"Unbekannter Befehl: "<<command.toStdString()<<'\n'; return 2;
    }
    bool timeoutOk=false; const auto timeout=parser.value("timeout").toInt(&timeoutOk);
    bool portOk=false; const auto port=parser.value("port").toUInt(&portOk);
    const auto host=parser.value("host").trimmed();
    if(!timeoutOk || timeout<1 || timeout>86400 || !portOk || port<1 || port>65535 || host.isEmpty()) {
        std::cerr<<"Ungültiger Server, TCP-Port oder Antwortfrist\n"; return 2;
    }
    try {
        QTcpSocket socket;
        socket.connectToHost(host,static_cast<quint16>(port));
        if(!socket.waitForConnected(3000)) throw std::runtime_error(socket.errorString().toStdString());
        if(command=="refresh") { writeLine(socket,{{"_airctrl","refresh"}}); return 0; }
        if(command=="set") {
            QJsonObject values;
            for(int i=1;i<positional.size();++i) {
                const auto split=positional[i].indexOf('=');
                if(split<1) throw std::runtime_error("set expects KEY=VALUE");
                const auto key=positional[i].left(split);
                values[key]=typedValue(key,positional[i].mid(split+1));
            }
            const auto problem=controlValuesError(values);
            if(!problem.isEmpty()) throw std::runtime_error(problem.toStdString());
            writeLine(socket,{{"_airctrl","control"},{"id",1},{"values",values}});
        }
        QByteArray buffer;
        for(;;) {
            const auto envelope=nextLine(socket,buffer,timeout*1000);
            const auto kind=envelope.value("_airctrl").toString();
            if(command=="server-status" && kind=="state") { print(envelope,!parser.isSet("json")); return 0; }
            if((command=="status" || command=="watch") && kind=="status") {
                print(envelope.value("data").toObject(),!parser.isSet("json"));
                if(command=="status") return 0;
            }
            if(command=="set" && kind=="control" && envelope.value("id").toInt()==1) {
                print(envelope,!parser.isSet("json"));
                return envelope.value("ok").toBool()?0:1;
            }
            if(kind=="error") throw std::runtime_error(envelope.value("error").toString().toStdString());
        }
    } catch(const std::exception& error) {
        std::cerr<<"AirControl: "<<error.what()<<'\n'; return 1;
    }
}
