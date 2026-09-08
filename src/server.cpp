#include "controlvalues.hpp"
#include "ipc.hpp"
#include "airctrl_version.hpp"
#include <aioairctrl/client.hpp>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {
struct DeviceConfig {
    QString host = "AC2729-10";
    int port = 5683;
    int reconnectMs = 10000;
    int requestMs = 60000;
    int idleMs = 90000;
    bool operator==(const DeviceConfig& other) const {
        return host == other.host && port == other.port && reconnectMs == other.reconnectMs &&
               requestMs == other.requestMs && idleMs == other.idleMs;
    }
    bool operator!=(const DeviceConfig& other) const { return !(*this == other); }
};

struct DeviceCommand {
    quint64 client = 0;
    quint64 id = 0;
    QJsonObject values;
};

class AirCtrlServer final : public QObject {
    Q_OBJECT
public:
    AirCtrlServer(QString socketPath, DeviceConfig config, QObject* parent = nullptr)
        : QObject(parent), socketPath_(std::move(socketPath)), config_(std::move(config)) {
        connect(&listener_, &QLocalServer::newConnection, this, &AirCtrlServer::acceptClients);
    }
    ~AirCtrlServer() override { shutdown(); }

    bool start(QString* error) {
        const QFileInfo endpoint(socketPath_);
        if (!QDir().mkpath(endpoint.absolutePath())) {
            if (error) *error = "IPC-Verzeichnis konnte nicht angelegt werden: " + endpoint.absolutePath();
            return false;
        }
        QFile::setPermissions(endpoint.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                       QFileDevice::ExeOwner);
        instanceLock_ = std::make_unique<QLockFile>(socketPath_ + ".lock");
        instanceLock_->setStaleLockTime(30000);
        if (!instanceLock_->tryLock(0)) {
            if (error) *error = "AirControl-Server läuft bereits.";
            return false;
        }
        QLocalServer::removeServer(socketPath_);
        if (!listener_.listen(socketPath_)) {
            if (error) *error = listener_.errorString();
            return false;
        }
        QFile::setPermissions(socketPath_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
        QLocalServer::removeServer(socketPath_);
    }

    void acceptClients() {
        while (auto* socket = listener_.nextPendingConnection()) {
            const auto id = nextClient_++;
            clients_.insert(id, socket);
            buffers_.insert(id, {});
            socket->setProperty("airctrlClient", QVariant::fromValue<qulonglong>(id));
            connect(socket, &QLocalSocket::readyRead, this, [this, id] { readClient(id); });
            connect(socket, &QLocalSocket::disconnected, this, [this, id] {
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
            socket->disconnectFromServer();
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
            DeviceConfig config;
            config.host = request.value("host").toString().trimmed();
            config.port = request.value("port").toInt(5683);
            config.reconnectMs = qBound(50, request.value("reconnect_ms").toInt(10000), 300000);
            config.requestMs = qBound(50, request.value("request_ms").toInt(60000), 86400000);
            config.idleMs = qBound(50, request.value("idle_ms").toInt(90000), 86400000);
            if (config.host.isEmpty() || config.port < 1 || config.port > 65535) {
                send(client, {{"_airctrl", "error"}, {"error", "Ungültiger Hostname oder UDP-Port."}});
                return;
            }
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (config != config_) {
                    config_ = config;
                    ++configGeneration_;
                    reconnectRequested_ = true;
                    changed = true;
                }
            }
            if (changed) {
                deviceReady_.store(false);
                DeviceCommand pending;
                while (takeCommand(&pending))
                    postControl(pending, false, "Gerätekonfiguration geändert; Befehl nicht ausgeführt.");
                wake_.notify_all();
            }
            send(client, {{"_airctrl", "configured"}, {"host", config.host}, {"port", config.port}});
            if (!changed && state_ == "connected" && !lastStatus_.isEmpty())
                send(client, {{"_airctrl", "status"}, {"data", lastStatus_}});
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
        if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
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

    QString socketPath_;
    QLocalServer listener_;
    std::unique_ptr<QLockFile> instanceLock_;
    QHash<quint64, QLocalSocket*> clients_;
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
    parser.setApplicationDescription("Lokaler AirControl-Server; einziger Prozess mit AC2729-Zugriff");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {{"H", "host"}, "Hostname oder IP-Adresse des Geräts", "host", "AC2729-10"},
        {{"P", "port"}, "UDP-Port", "port", "5683"},
        {"socket", "Lokaler Unix-Socket", "path"},
        {"reconnect", "Wiederverbindung nach Fehler in Sekunden", "seconds", "10"},
        {"reconnect-ms", "Wiederverbindung in Millisekunden", "milliseconds"},
        {"request-ms", "CoAP-Anlauffrist in Millisekunden", "milliseconds", "60000"},
        {"idle-ms", "Status-Stillstandsfrist in Millisekunden", "milliseconds", "90000"},
    });
    parser.process(app);
    bool portOk = false, reconnectOk = false;
    DeviceConfig config;
    config.host = parser.value("host").trimmed();
    config.port = parser.value("port").toInt(&portOk);
    config.reconnectMs = parser.value("reconnect").toInt(&reconnectOk) * 1000;
    if (parser.isSet("reconnect-ms")) config.reconnectMs = parser.value("reconnect-ms").toInt(&reconnectOk);
    bool requestOk=false,idleOk=false;
    config.requestMs=parser.value("request-ms").toInt(&requestOk);
    config.idleMs=parser.value("idle-ms").toInt(&idleOk);
    if (config.host.isEmpty() || !portOk || config.port < 1 || config.port > 65535 ||
        !reconnectOk || config.reconnectMs < 50 || config.reconnectMs > 300000 ||
        !requestOk || config.requestMs<50 || config.requestMs>86400000 ||
        !idleOk || config.idleMs<50 || config.idleMs>86400000) {
        qCritical("Ungültiger Host, Port oder Wiederverbindungswert.");
        return 2;
    }
    const auto socket = parser.isSet("socket") ? parser.value("socket") : airctrlSocketPath();
    AirCtrlServer server(socket, config);
    QString error;
    if (!server.start(&error)) {
        qCritical().noquote() << "AirControl-Server konnte nicht starten:" << error;
        return 1;
    }
    return app.exec();
}

#include "server.moc"
