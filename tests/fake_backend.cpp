#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments(); args.removeFirst();
    QFile log(qEnvironmentVariable("AIRCTRL_TEST_LOG"));
    if (log.open(QIODevice::Append)) {
        log.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) + '\n');
        log.close();
    }
    const auto mode = qEnvironmentVariable("AIRCTRL_TEST_MODE");
    const auto gate = qEnvironmentVariable("AIRCTRL_TEST_READ_GATE");
    if (args.contains("status") && !gate.isEmpty()) {
        QElapsedTimer deadline; deadline.start();
        while (!QFileInfo::exists(gate)) {
            if (deadline.elapsed() > 3000) { std::cerr << "test read gate timed out\n"; return 1; }
            QThread::msleep(10);
        }
    }
    if (mode == "failure-once") {
        QFile records(qEnvironmentVariable("AIRCTRL_TEST_LOG"));
        records.open(QIODevice::ReadOnly);
        if (records.readAll().count('\n') == 1) { std::cerr << "Error: CoAP response timed out\n"; return 1; }
    }
    if (mode == "timeout") QThread::msleep(3000);
    if (mode == "failure") { std::cerr << "device offline\n"; return 1; }
    if (mode == "bad-json") { std::cout << "not JSON\n"; return 0; }
    if (args.contains("set")) {
        if (mode == "write-failure") { std::cerr << "Error: CoAP response timed out\n"; return 1; }
        QFile state(qEnvironmentVariable("AIRCTRL_TEST_STATE"));
        QJsonObject object;
        if (state.open(QIODevice::ReadOnly)) { object = QJsonDocument::fromJson(state.readAll()).object(); state.close(); }
        for (const auto& arg : args) {
            const auto split=arg.indexOf('=');
            if(split<1) continue;
            const auto key=arg.left(split), value=arg.mid(split+1);
            if(args.contains("-I")) object[key]=value.toInt();
            else if(value=="true" || value=="false") object[key]=value=="true";
            else object[key]=value;
        }
        if (state.open(QIODevice::WriteOnly)) state.write(QJsonDocument(object).toJson());
        return 0;
    }
    QJsonObject object{{"name", "Wohnzimmer"}, {"modelid", "AC2729/10"}, {"pwr", "1"},
                       {"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"iaql",1},
                       {"mode","P"},{"om","s"},{"func","PH"},{"cl",false},{"aqil",100},{"uil","1"},{"dt",0}};
    QFile state(qEnvironmentVariable("AIRCTRL_TEST_STATE"));
    if (state.open(QIODevice::ReadOnly)) {
        auto saved = QJsonDocument::fromJson(state.readAll()).object();
        for (auto i = saved.begin(); i != saved.end(); ++i) object[i.key()] = i.value();
    }
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << '\n';
}
