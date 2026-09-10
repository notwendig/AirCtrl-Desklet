/**
 * @file server.cpp
 * @brief Multi-client TCP server and sole owner of the Philips UDP session.
 */
#include "controlvalues.hpp"
#include "ipc.hpp"
#include "airctrl_version.hpp"
#include <aioairctrl/client.hpp>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {
/** @brief Validated device endpoint and timeout policy from /etc/airctrld.cfg. */
struct DeviceConfig {
    QString host = "AC2729-10";
    int port = 5683;
    int reconnectMs = 10000;
    int requestMs = 60000;
    int idleMs = 90000;
};

/** @brief Validated listener and device configuration used for one process run. */
struct ServerConfig {
    QHostAddress listenAddress{QHostAddress::Any};
    quint16 listenPort=5680;
    DeviceConfig device;
};

/** @brief Parse the mandatory system configuration without accepting client overrides. */
bool loadConfig(const QString& path,ServerConfig* config,QString* error) {
    if(!QFileInfo::exists(path) || !QFileInfo(path).isFile()) {
        if(error) *error="Konfiguration fehlt: "+path;
        return false;
    }
    QSettings file(path,QSettings::IniFormat);
    if(file.status()!=QSettings::NoError) {
        if(error) *error="Konfiguration kann nicht gelesen werden: "+path;
        return false;
    }
    const auto addressText=file.value("server/listen_address","0.0.0.0").toString().trimmed();
    QHostAddress address;
    bool listenPortOk=false,devicePortOk=false,reconnectOk=false,requestOk=false,idleOk=false;
    const auto listenPort=file.value("server/port",5680).toUInt(&listenPortOk);
    DeviceConfig device;
    device.host=file.value("device/host","AC2729-10").toString().trimmed();
    device.port=file.value("device/port",5683).toInt(&devicePortOk);
    device.reconnectMs=file.value("device/reconnect_ms",10000).toInt(&reconnectOk);
    device.requestMs=file.value("device/request_ms",60000).toInt(&requestOk);
    device.idleMs=file.value("device/idle_ms",90000).toInt(&idleOk);
    if(!address.setAddress(addressText) || !listenPortOk || listenPort<1 || listenPort>65535 ||
       device.host.isEmpty() || !devicePortOk || device.port<1 || device.port>65535 ||
       !reconnectOk || device.reconnectMs<50 || device.reconnectMs>300000 ||
       !requestOk || device.requestMs<50 || device.requestMs>86400000 ||
       !idleOk || device.idleMs<50 || device.idleMs>86400000) {
        if(error) *error="Ungültiger Wert in "+path;
        return false;
    }
    config->listenAddress=address;
    config->listenPort=static_cast<quint16>(listenPort);
    config->device=std::move(device);
    return true;
}

/** @brief One validated control request correlated to its originating TCP client. */
struct DeviceCommand {
    quint64 client = 0;
    quint64 id = 0;
    QJsonObject values;
};

/**
 * @brief Bridges many untrusted local-network TCP clients to one device session.
 *
 * Qt sockets remain on the main thread. The blocking Philips client lives on
 * one worker thread; queued invocations carry status and results back across
 * that boundary. The command queue is serialized globally, not per client.
 */
class AirCtrlServer final : public QObject {
    Q_OBJECT
public:
    AirCtrlServer(QHostAddress listenAddress,quint16 listenPort,DeviceConfig config,QObject* parent = nullptr)
        : QObject(parent),listenAddress_(std::move(listenAddress)),listenPort_(listenPort),config_(std::move(config)) {
        connect(&listener_, &QTcpServer::newConnection, this, &AirCtrlServer::acceptClients);
    }
    ~AirCtrlServer() override { shutdown(); }

    bool start(QString* error) {
        if (!listener_.listen(listenAddress_,listenPort_)) {
            if (error) *error = listener_.errorString();
            return false;
        }
        worker_ = std::thread([this] { deviceLoop(); });
        // Tests may stop a controller before its first connection completes.
        // Do not leave that detached, never-used test server behind forever.
        if (qEnvironmentVariableIntValue("AIRCTRL_SERVER_EXIT_ON_IDLE") == 1)
            QTimer::singleShot(5000, this, [this] {
                if (clients_.isEmpty()) QCoreApplication::quit();
            });
        return true;
    }

private:
    void shutdown() {
        if (stopping_.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            reconnectRequested_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        listener_.close();
    }

    void acceptClients() {
        while (auto* socket = listener_.nextPendingConnection()) {
            const auto id = nextClient_++;
            clients_.insert(id, socket);
            buffers_.insert(id, {});
            socket->setProperty("airctrlClient", QVariant::fromValue<qulonglong>(id));
            connect(socket, &QTcpSocket::readyRead, this, [this, id] { readClient(id); });
            connect(socket, &QTcpSocket::disconnected, this, [this, id] {
                if (auto* old = clients_.take(id)) old->deleteLater();
                buffers_.remove(id);
                if (clients_.isEmpty() && qEnvironmentVariableIntValue("AIRCTRL_SERVER_EXIT_ON_IDLE") == 1)
                    QCoreApplication::quit();
            });
            send(id, stateEnvelope());
            if (state_ == "connected" && !lastStatus_.isEmpty())
                send(id, {{"_airctrl", "status"}, {"data", lastStatus_}});
        }
    }

    void readClient(quint64 client) {
        auto* socket = clients_.value(client);
        if (!socket) return;
        auto& buffer = buffers_[client];
        buffer += socket->readAll();
        if (buffer.size() > 1024 * 1024) {
            send(client, {{"_airctrl", "error"}, {"error", "IPC-Nachricht ist zu groß."}});
            socket->disconnectFromHost();
            return;
        }
        for (;;) {
            const auto newline = buffer.indexOf('\n');
            if (newline < 0) break;
            const auto line = buffer.left(newline).trimmed();
            buffer.remove(0, newline + 1);
            if (line.isEmpty()) continue;
            const auto document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                send(client, {{"_airctrl", "error"}, {"error", "Ungültiges IPC-JSON."}});
                continue;
            }
            handle(client, document.object());
        }
    }

    void handle(quint64 client, const QJsonObject& request) {
        const auto kind = request.value("_airctrl").toString();
        if (kind == "configure") {
            send(client, {{"_airctrl", "error"},
                {"error", "Geräteeinstellungen gehören ausschließlich in /etc/airctrld.cfg."}});
            return;
        }
        if (kind == "refresh") {
            // Coalesce refresh requests already queued by this or other
            // clients. One event-loop turn must create only one new device
            // session and one new session key.
            if (refreshScheduled_) return;
            refreshScheduled_ = true;
            QTimer::singleShot(0, this, [this] {
                refreshScheduled_ = false;
                deviceReady_.store(false);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    reconnectRequested_ = true;
                }
                DeviceCommand pending;
                while (takeCommand(&pending))
                    postControl(pending, false, "Geräte-I/O wird erneuert; Befehl nicht ausgeführt.");
                wake_.notify_all();
            });
            return;
        }
        if (kind == "control") {
            const auto id = request.value("id").toVariant().toULongLong();
            const auto values = request.value("values").toObject();
            const auto problem = controlValuesError(values);
            if (id == 0 || !problem.isEmpty()) {
                send(client, {{"_airctrl", "control"}, {"id", static_cast<qint64>(id)},
                              {"ok", false}, {"error", problem.isEmpty() ? "Ungültige Befehlskennung." : problem}});
                return;
            }
            if (!deviceReady_.load()) {
                send(client, {{"_airctrl", "control"}, {"id", static_cast<qint64>(id)},
                              {"ok", false}, {"error", "Der Server hat noch keine aktive Geräteverbindung."}});
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                commands_.push_back({client, id, values});
            }
            wake_.notify_all();
            return;
        }
        if (kind == "ping") {
            send(client, {{"_airctrl", "pong"}});
            return;
        }
        send(client, {{"_airctrl", "error"}, {"error", "Unbekannter IPC-Befehl."}});
    }

    void send(quint64 client, const QJsonObject& object) {
        auto* socket = clients_.value(client);
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    }
    void broadcast(const QJsonObject& object) {
        const auto ids = clients_.keys();
        for (const auto id : ids) send(id, object);
    }
    QJsonObject stateEnvelope() const {
        QJsonObject object{{"_airctrl", "state"}, {"state", state_},
                           {"starts", static_cast<qint64>(starts_)}};
        if (!stateError_.isEmpty()) object["error"] = stateError_;
        return object;
    }
    void postState(QString state, QString error = {}) {
        QMetaObject::invokeMethod(this, [this, state = std::move(state), error = std::move(error)] {
            state_ = state;
            stateError_ = error;
            broadcast(stateEnvelope());
        }, Qt::QueuedConnection);
    }
    void postStart() {
        QMetaObject::invokeMethod(this, [this] {
            ++starts_;
            state_ = "connecting";
            stateError_.clear();
            broadcast(stateEnvelope());
        }, Qt::QueuedConnection);
    }
    /** @brief Transfer an immutable device snapshot back to the Qt thread. */
    void postStatus(const aioairctrl::Json& status) {
        const auto bytes = QByteArray::fromStdString(status.dump());
        QMetaObject::invokeMethod(this, [this, bytes] {
            const auto document = QJsonDocument::fromJson(bytes);
            if (!document.isObject()) return;
            lastStatus_ = document.object();
            state_ = "connected";
            stateError_.clear();
            broadcast({{"_airctrl", "status"}, {"data", lastStatus_}});
        }, Qt::QueuedConnection);
    }
    void postControl(DeviceCommand command, bool ok, QString error = {}) {
        QMetaObject::invokeMethod(this, [this, command = std::move(command), ok, error = std::move(error)] {
            QJsonObject result{{"_airctrl", "control"}, {"id", static_cast<qint64>(command.id)}, {"ok", ok}};
            if (!error.isEmpty()) result["error"] = error;
            send(command.client, result);
        }, Qt::QueuedConnection);
    }
    bool interrupted(std::uint64_t generation, bool commandsInterrupt = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopping_.load() || reconnectRequested_ || configGeneration_ != generation ||
               (commandsInterrupt && !commands_.empty());
    }
    bool waitReconnect(int milliseconds, std::uint64_t generation) {
        std::unique_lock<std::mutex> lock(mutex_);
        return wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] {
            return stopping_.load() || reconnectRequested_ || configGeneration_ != generation || !commands_.empty();
        });
    }
    bool takeCommand(DeviceCommand* command) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (commands_.empty()) return false;
        *command = std::move(commands_.front());
        commands_.pop_front();
        return true;
    }
    /**
     * @brief Own the complete lifetime of every Philips UDP/CoAP session.
     *
     * Leaving an outer iteration destroys the previous device client and its
     * socket. The following iteration creates and synchronizes a fresh session
     * while all TCP clients remain attached to this server process.
     */
    void deviceLoop() {
        while (!stopping_.load()) {
            DeviceConfig config;
            std::uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                config = config_;
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
                aioairctrl::Client device(config.host.toStdString(), options);
                bool statusRequiredAfterControl = false;
                while (!stopping_.load()) {
                    if (interrupted(generation, !statusRequiredAfterControl)) {
                        bool reconnect = false;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            reconnect = reconnectRequested_ || configGeneration_ != generation;
                        }
                        if (reconnect) break;
                    }
                    device.observe_status(
                        [this, &statusRequiredAfterControl](const aioairctrl::Json& status) {
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
                        std::lock_guard<std::mutex> lock(mutex_);
                        reconnect = reconnectRequested_ || configGeneration_ != generation;
                    }
                    if (reconnect || stopping_.load()) break;

                    DeviceCommand command;
                    if (takeCommand(&command)) {
                        // One server-wide control at a time. Before a second
                        // client's request may run, Observe must deliver the
                        // status following this attempt.
                        statusRequiredAfterControl = true;
                        try {
                            const auto json = aioairctrl::Json::parse(
                                QJsonDocument(command.values).toJson(QJsonDocument::Compact).constData());
                            const bool accepted = device.set_control_values(json, 0, false);
                            bool configurationChanged = false;
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                configurationChanged = reconnectRequested_ || configGeneration_ != generation;
                            }
                            if (configurationChanged) {
                                postControl(command, false,
                                    "Geräte-I/O wurde während des Schaltbefehls erneuert · Ausgang unbekannt; keine Wiederholung.");
                            } else {
                                postControl(command, accepted,
                                            accepted ? QString{} : QString{"Gerät hat den Schaltbefehl abgelehnt."});
                            }
                        } catch (const std::exception& error) {
                            postControl(command, false, QString::fromUtf8(error.what()));
                        }
                    }
                }
            } catch (const std::exception& error) {
                deviceReady_.store(false);
                postState("error", QString::fromUtf8(error.what()));
                DeviceCommand command;
                while (takeCommand(&command))
                    postControl(command, false, "Geräte-I/O wurde unterbrochen; Befehl nicht wiederholt.");
                if (!stopping_.load()) waitReconnect(config.reconnectMs, generation);
            }
            deviceReady_.store(false);
        }
    }

    QHostAddress listenAddress_;
    quint16 listenPort_;
    QTcpServer listener_;
    QHash<quint64, QTcpSocket*> clients_;
    QHash<quint64, QByteArray> buffers_;
    quint64 nextClient_ = 1;
    QString state_ = "starting", stateError_;
    quint64 starts_ = 0;
    QJsonObject lastStatus_;

    std::atomic<bool> stopping_{false}, deviceReady_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    DeviceConfig config_;
    std::uint64_t configGeneration_ = 1;
    bool reconnectRequested_ = false;
    bool refreshScheduled_ = false;
    std::deque<DeviceCommand> commands_;
    std::thread worker_;
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("airctrl-server");
    QCoreApplication::setApplicationVersion(AIRCTRL_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("AirControl-TCP-Server; einziger Prozess mit AC2729-Zugriff");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"config","Serverkonfiguration","file","/etc/airctrld.cfg"});
    parser.addOption({"check-config","Konfiguration prüfen und beenden"});
    parser.process(app);
    auto configPath=parser.value("config");
    const auto testConfig=qEnvironmentVariable("AIRCTRL_TEST_SERVER_CONFIG").trimmed();
    if(!testConfig.isEmpty()) configPath=testConfig;
    ServerConfig config;
    QString error;
    if(!loadConfig(configPath,&config,&error)) {
        qCritical().noquote()<<error;
        return 2;
    }
    if(parser.isSet("check-config")) {
        qInfo().noquote()<<"Konfiguration gültig:"<<configPath;
        return 0;
    }
    AirCtrlServer server(config.listenAddress,config.listenPort,config.device);
    if (!server.start(&error)) {
        qCritical().noquote() << "AirControl-Server konnte nicht starten:" << error;
        return 1;
    }
    return app.exec();
}

#include "server.moc"
