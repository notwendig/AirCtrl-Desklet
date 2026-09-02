#include "controller.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFileInfo>

Controller::Controller(QString executable, QObject* parent)
    : QObject(parent), executable_(std::move(executable)) {
    poll_.setSingleShot(true);
    verify_.setSingleShot(true);
    readRetry_.setSingleShot(true);
    watchdog_.setSingleShot(true);
    connect(&poll_, &QTimer::timeout, this, &Controller::refresh);
    connect(&verify_, &QTimer::timeout, this, &Controller::refresh);
    connect(&readRetry_, &QTimer::timeout, this, [this] {
        if (active_) launch(Operation::Read, {"status", "-J"});
    });
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_.kill();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        output_ += process_.readAllStandardOutput();
        if (output_.size() > 1024 * 1024) { oversized_ = true; process_.kill(); }
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        error_ += process_.readAllStandardError();
        if (error_.size() > 64 * 1024) { oversized_ = true; process_.kill(); }
    });
    connect(&process_, &QProcess::finished, this, &Controller::finished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            watchdog_.stop();
            setBusy(false);
            fail("Geräteprogramm konnte nicht gestartet werden: " + process_.errorString());
        }
    });
}
Controller::~Controller() {
    stop();
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}
void Controller::configure(QString host, int port, int intervalSeconds) {
    host_ = host.trimmed();
    port_ = port;
    intervalMs_ = qBound(5, intervalSeconds, 300) * 1000;
}
void Controller::setWatchdogInterval(int milliseconds) { watchdogMs_ = qMax(50, milliseconds); }
void Controller::start() { active_ = true; refresh(); }
void Controller::stop() {
    active_ = false;
    queuedWrite_.clear();
    poll_.stop(); verify_.stop(); watchdog_.stop(); readRetry_.stop();
    if (process_.state() != QProcess::NotRunning) process_.kill();
}
void Controller::setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy);
}
void Controller::refresh() {
    if (busy_) return;
    readAttempt_ = 0;
    launch(Operation::Read, {"status", "-J"});
}
void Controller::setPower(bool on) {
    launch(Operation::Write, {"set", on ? "pwr=1" : "pwr=0"});
}
void Controller::setHumidity(int percent) {
    if (percent != 40 && percent != 50 && percent != 60 && percent != 70) {
        emit failed("Erlaubte Zielfeuchte: 40, 50, 60 oder 70 %.");
        return;
    }
    launch(Operation::Write, {"set", "-I", "rhset=" + QString::number(percent)});
}
void Controller::setPanelValues(const QJsonObject& values) {
    if (values.isEmpty()) return;
    bool integers = values.begin().value().isDouble();
    QStringList args{"set"};
    if (integers) args << "-I";
    for (auto i = values.begin(); i != values.end(); ++i) {
        const auto v = i.value();
        const auto s = v.toString();
        const auto n = v.toDouble(-1);
        const bool valid =
            (i.key() == "pwr" && v.isString() && (s == "0" || s == "1")) ||
            (i.key() == "cl" && v.isBool()) ||
            (i.key() == "mode" && v.isString() && QStringList{"P","A","S","M"}.contains(s)) ||
            (i.key() == "om" && v.isString() && QStringList{"1","2","3","s","t"}.contains(s)) ||
            (i.key() == "func" && v.isString() && (s == "P" || s == "PH")) ||
            (i.key() == "uil" && v.isString() && (s == "0" || s == "1")) ||
            (i.key() == "rhset" && v.isDouble() && (n == 40 || n == 50 || n == 60 || n == 70)) ||
            (i.key() == "aqil" && v.isDouble() && (n == 0 || n == 25 || n == 50 || n == 75 || n == 100)) ||
            (i.key() == "dt" && v.isDouble() && n >= 0 && n <= 12 && n == int(n));
        if (!valid || v.isDouble() != integers) {
            emit failed("Ungültiger Steuerwert: " + i.key());
            return;
        }
        args << i.key() + "=" + (v.isBool() ? (v.toBool() ? "true" : "false") :
                                 integers ? QString::number(v.toInt()) : s);
    }
    launch(Operation::Write, args);
}
void Controller::launch(Operation operation, const QStringList& tail) {
    if (busy_) {
        // Keep the read and write serialized. The UI blocks further commands
        // once one has been submitted, but background reads do not block clicks.
        if (operation == Operation::Write && operation_ == Operation::Read && queuedWrite_.isEmpty())
            queuedWrite_ = tail;
        return;
    }
    poll_.stop(); verify_.stop(); readRetry_.stop();
    operation_ = operation;
    if (host_.isEmpty() || port_ < 1 || port_ > 65535) { fail("Ungültige Geräteadresse."); return; }
    if (!QFileInfo(executable_).isExecutable()) { fail("Geräteprogramm fehlt: " + executable_); return; }
    output_.clear(); error_.clear();
    timedOut_ = false; oversized_ = false;
    QStringList args{"-H", host_, "-P", QString::number(port_), "--timeout", "10",
                     "--retries", "0", "--no-resync"};
    args += tail;
    setBusy(true);
    process_.start(executable_, args); // argument list: no shell interpretation
    watchdog_.start(watchdogMs_);
}
void Controller::fail(const QString& reason) {
    // A failed read must not leave a command waiting for some future reconnect.
    queuedWrite_.clear();
    emit failed(reason);
    if (!active_) return;
    if (operation_ == Operation::Read && readAttempt_ == 0) {
        ++readAttempt_;
        readRetry_.start(1000); // repeat a read once; never repeat a write
    } else poll_.start(intervalMs_);
}
void Controller::finished(int code, QProcess::ExitStatus exitStatus) {
    watchdog_.stop();
    output_ += process_.readAllStandardOutput();
    error_ += process_.readAllStandardError();
    setBusy(false);
    if (!active_) return;
    if (timedOut_) { fail("Keine Antwort vom Gerät. Neuer Versuch folgt automatisch."); return; }
    if (oversized_) { fail("Die Geräteantwort ist zu groß."); return; }
    if (code != 0 || exitStatus != QProcess::NormalExit) {
        auto detail = QString::fromUtf8(error_).trimmed().left(600);
        fail(detail.isEmpty() ? "Gerät nicht erreichbar oder Befehl abgelehnt." : detail);
        return;
    }
    if (operation_ == Operation::Write) {
        emit controlAccepted();
        verify_.start(650); // only a fresh status updates displayed control state
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(output_, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || document.object().isEmpty()) {
        fail("Ungültige Statusantwort: kein gültiges JSON-Objekt.");
        return;
    }
    if (!queuedWrite_.isEmpty()) {
        const auto command = queuedWrite_;
        queuedWrite_.clear();
        // This reply predates the command and cannot confirm it. The fresh read
        // scheduled after the write will update values and release the UI lock.
        launch(Operation::Write, command);
        return;
    }
    emit statusReceived(document.object());
    if (active_) poll_.start(intervalMs_);
}
