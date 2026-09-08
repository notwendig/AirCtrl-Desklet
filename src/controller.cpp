#include "controller.hpp"
#include "controlvalues.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFileInfo>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <unistd.h>
#include <csignal>
#endif

Controller::Controller(QString executable, QObject* parent)
    : QObject(parent), executable_(std::move(executable)) {
#ifdef Q_OS_LINUX
    // The persistent I/O session must not survive pkill/crash of the widget. This
    // also covers termination before Qt can run Controller's destructor.
    const auto parentPid=::getpid();
    observer_.setChildProcessModifier([parentPid] {
        if(::prctl(PR_SET_PDEATHSIG,SIGTERM)<0 || ::getppid()!=parentPid) ::_exit(127);
    });
#endif
    for(auto* timer : {&reconnect_, &observationWatchdog_, &observerStopWatchdog_, &writeWatchdog_, &confirmation_})
        timer->setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, &Controller::launchObserver);
    connect(&observationWatchdog_, &QTimer::timeout, this, [this] {
        abortObserver(hasStatus_ ? "Längere Zeit keine gültige Statusmeldung · Verbindung wird neu aufgebaut."
                                 : "Keine erste Statusmeldung innerhalb der Anlaufzeit · neuer Verbindungsversuch folgt.");
    });
    connect(&observerStopWatchdog_, &QTimer::timeout, &observer_, &QProcess::kill);
    connect(&writeWatchdog_, &QTimer::timeout, this, [this] {
        failCommand("Keine Antwort auf den Schaltbefehl · Ausgang unbekannt; keine automatische Wiederholung.");
    });
    connect(&confirmation_, &QTimer::timeout, this, [this] {
        failCommand("Keine neue Statusbestätigung nach dem Schaltbefehl · keine automatische Wiederholung.");
    });
    connect(&observer_, &QProcess::readyReadStandardOutput, this, &Controller::readObserver);
    connect(&observer_, &QProcess::readyReadStandardError, this, [this] {
        observerError_=(observerError_+observer_.readAllStandardError()).right(64*1024);
        if(!observerStopping_ && active_) progress_=QString::fromUtf8(observerError_).trimmed().section('\n',-1);
    });
    connect(&observer_, &QProcess::finished, this, &Controller::observerFinished);
    connect(&observer_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart) {
            observationWatchdog_.stop(); observerStopWatchdog_.stop(); observerStopping_=false;
            if(active_) observationFailed("Statusprogramm konnte nicht gestartet werden: "+observer_.errorString());
        }
    });
}
Controller::~Controller() {
    stop();
    observer_.disconnect(this);
    if(observer_.state()!=QProcess::NotRunning) { observer_.kill(); observer_.waitForFinished(1000); }
}
void Controller::configure(QString host, int port, int reconnectSeconds) {
    host_=host.trimmed(); port_=port; reconnectMs_=qBound(5,reconnectSeconds,300)*1000;
}
void Controller::setWatchdogInterval(int ms) { writeMs_=qMax(50,ms); }
void Controller::setObservationWatchdogs(int startup, int idle) { startupMs_=qMax(50,startup); idleMs_=qMax(50,idle); }
void Controller::setConfirmationTimeout(int ms) { confirmationMs_=qMax(50,ms); }
void Controller::setReconnectDelay(int ms) { reconnectMs_=qMax(50,ms); }
void Controller::setBusy(bool busy) {
    if(busy_==busy) return;
    busy_=busy; emit busyChanged(busy);
}
QString Controller::addressError() const {
    if(host_.isEmpty() || port_<1 || port_>65535) return "Ungültiger Hostname oder ungültige IP-Adresse.";
    if(!QFileInfo(executable_).isExecutable()) return "Geräteprogramm fehlt: "+executable_;
    return {};
}
void Controller::start() {
    active_=true;
    if(observerStopping_) restartObserver_=true;
    else if(observer_.state()==QProcess::NotRunning) launchObserver();
}
void Controller::stopObserver() {
    observationWatchdog_.stop(); hasStatus_=false;
    if(observer_.state()==QProcess::NotRunning) return;
    observerStopping_=true;
    observer_.terminate(); // SIGTERM lets the session unsubscribe and close its sole socket
    observerStopWatchdog_.start(1000);
}
void Controller::stop() {
    active_=false; restartObserver_=false;
    reconnect_.stop(); confirmation_.stop(); writeWatchdog_.stop();
    awaitingConfirmation_=false; pendingCommandId_=0; stopObserver();
    setBusy(false);
}
void Controller::refresh() {
    if(!active_ || busy_) return;
    reconnect_.stop();
    if(observer_.state()==QProcess::NotRunning) launchObserver();
    else { restartObserver_=true; stopObserver(); }
}
void Controller::launchObserver() {
    if(!active_ || observer_.state()!=QProcess::NotRunning) return;
    reconnect_.stop(); observerStopWatchdog_.stop();
    restartObserver_=false; observerStopping_=false; hasStatus_=false;
    stream_.clear(); observerError_.clear(); observerProblem_.clear();
    const auto error=addressError();
    if(!error.isEmpty()) { observationFailed(error); return; }
    progress_="Dauerhafte I/O-Sitzung wird gestartet"; ++observationStarts_;
    observer_.start(executable_, {"-H",host_,"-P",QString::number(port_),"-D",
        "--timeout",QString::number(ObserveRequestSeconds),"--control-timeout","10",
        "--idle-timeout",QString::number(ObserveIdleSeconds),"session","-J"});
    observationWatchdog_.start(startupMs_);
}
void Controller::abortObserver(const QString& reason) {
    if(!active_ || observerStopping_ || !observerProblem_.isEmpty()) return;
    observerProblem_=reason; observationWatchdog_.stop();
    observer_.kill(); // finished() reports the failure once
}
void Controller::readObserver() {
    if(!observer_.isOpen()) return;
    const auto bytes=observer_.readAllStandardOutput();
    if(!active_ || observerStopping_ || !observerProblem_.isEmpty()) return;
    stream_+=bytes;
    for(;;) {
        const auto newline=stream_.indexOf('\n');
        if(newline<0) break;
        if(newline>1024*1024) { abortObserver("Statuszeile ist zu groß."); return; }
        const auto line=stream_.left(newline).trimmed(); stream_.remove(0,newline+1);
        if(line.isEmpty()) continue;
        QJsonParseError error;
        const auto document=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError || !document.isObject() || document.object().isEmpty()) {
            abortObserver("Ungültige Statusmeldung: kein gültiges JSON-Objekt."); return;
        }
        const auto envelope=document.object();
        const auto kind=envelope.value("_airctrl").toString();
        if(kind=="control") {
            const auto id=envelope.value("id").toVariant().toULongLong();
            if(!busy_ || id==0 || id!=pendingCommandId_) continue; // late/foreign reply
            writeWatchdog_.stop(); pendingCommandId_=0;
            if(!envelope.value("ok").toBool()) {
                auto reason=envelope.value("error").toString().trimmed();
                failCommand(reason.isEmpty() ? "Schaltbefehl abgelehnt oder nicht bestätigt." : reason);
                continue;
            }
            awaitingConfirmation_=true; confirmation_.start(confirmationMs_);
            emit controlAccepted();
            continue;
        }
        if(kind!="status" || !envelope.value("data").isObject() || envelope.value("data").toObject().isEmpty()) {
            abortObserver("Ungültige Meldung der I/O-Sitzung."); return;
        }
        const auto status=envelope.value("data").toObject();
        hasStatus_=true; ++statusCount_; progress_="Dauerhafte I/O-Sitzung aktiv";
        observationWatchdog_.start(idleMs_);
        emit statusPacketReceived();
        if(!active_ || observerStopping_) return;
        // Status received before the control ACK cannot confirm that command.
        if(busy_ && !awaitingConfirmation_) continue;
        if(awaitingConfirmation_) { awaitingConfirmation_=false; confirmation_.stop(); setBusy(false); }
        emit statusReceived(status);
        if(!active_ || observerStopping_) return;
    }
    if(stream_.size()>1024*1024) abortObserver("Unvollständige Statuszeile ist zu groß.");
}
void Controller::observerFinished(int code, QProcess::ExitStatus exitStatus) {
    readObserver();
    observerError_=(observerError_+observer_.readAllStandardError()).right(64*1024);
    observationWatchdog_.stop(); observerStopWatchdog_.stop(); hasStatus_=false;
    if(observerStopping_ || !active_) {
        observerStopping_=false; stream_.clear();
        if(active_ && restartObserver_) launchObserver();
        return;
    }
    if(busy_) failCommand("I/O-Sitzung wurde während des Schaltbefehls beendet · Ausgang unbekannt; keine automatische Wiederholung.");
    auto reason=observerProblem_;
    if(reason.isEmpty()) {
        const auto detail=QString::fromUtf8(observerError_).trimmed().right(1500);
        reason=(code==0 && exitStatus==QProcess::NormalExit) ? "I/O-Sitzung wurde unerwartet beendet." :
            detail.isEmpty() ? "I/O-Sitzung fehlgeschlagen." : detail;
        if(!stream_.trimmed().isEmpty()) reason+="\nUnvollständige letzte Statuszeile.";
    }
    stream_.clear(); observationFailed(reason);
}
void Controller::observationFailed(const QString& reason) {
    hasStatus_=false; progress_="Warte auf Wiederverbindung";
    emit failed(reason);
    if(active_) reconnect_.start(reconnectMs_);
}
void Controller::setPower(bool on) { setPanelValues({{"pwr",on ? "1" : "0"}}); }
void Controller::setHumidity(int percent) { setPanelValues({{"rhset",percent}}); }
void Controller::setPanelValues(const QJsonObject& values) {
    if(busy_ || values.isEmpty()) return;
    const auto problem=controlValuesError(values);
    if(!problem.isEmpty()) { failCommand(problem); return; }
    launchWrite(values);
}
void Controller::launchWrite(const QJsonObject& values) {
    if(busy_) return;
    const auto error=addressError();
    if(!error.isEmpty()) { failCommand(error); return; }
    if(!active_) { failCommand("Verbindungssteuerung ist nicht gestartet."); return; }
    if(observer_.state()==QProcess::NotRunning) { failCommand("Keine aktive I/O-Sitzung zum Gerät."); return; }
    awaitingConfirmation_=false; pendingCommandId_=nextCommandId_++;
    const QJsonObject command{{"id",static_cast<qint64>(pendingCommandId_)},{"values",values}};
    const auto line=QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n';
    setBusy(true);
    if(observer_.write(line)<0) { failCommand("Schaltbefehl konnte nicht an die I/O-Sitzung übergeben werden."); return; }
    writeWatchdog_.start(writeMs_);
}
void Controller::failCommand(const QString& reason) {
    pendingCommandId_=0; awaitingConfirmation_=false; writeWatchdog_.stop(); confirmation_.stop(); setBusy(false);
    emit commandFailed(reason);
}
