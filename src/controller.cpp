#include "controller.hpp"
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
    // A persistent observer must not survive pkill/crash of the widget. This
    // also covers termination before Qt can run Controller's destructor.
    const auto parentPid=::getpid();
    for(auto* process : {&observer_, &writer_}) process->setChildProcessModifier([parentPid] {
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
        writerProblem_="Keine Antwort auf den Schaltbefehl · Ausgang unbekannt; keine automatische Wiederholung.";
        writer_.kill();
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
    connect(&writer_, &QProcess::readyReadStandardOutput, this, [this] {
        writerBytes_+=writer_.readAllStandardOutput().size();
        if(writerBytes_>1024*1024) { writerProblem_="Schreibantwort ist zu groß."; writer_.kill(); }
    });
    connect(&writer_, &QProcess::readyReadStandardError, this, [this] {
        writerError_=(writerError_+writer_.readAllStandardError()).right(64*1024);
    });
    connect(&writer_, &QProcess::finished, this, &Controller::writeFinished);
    connect(&writer_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart) {
            writeWatchdog_.stop();
            if(active_ && !writerStopping_) failCommand("Schaltprogramm konnte nicht gestartet werden: "+writer_.errorString());
            writerStopping_=false;
        }
    });
}
Controller::~Controller() {
    stop();
    for(auto* process : {&observer_, &writer_}) {
        process->disconnect(this);
        if(process->state()!=QProcess::NotRunning) { process->kill(); process->waitForFinished(1000); }
    }
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
    if(host_.isEmpty() || port_<1 || port_>65535) return "Ungültige Geräteadresse.";
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
    observer_.terminate(); // SIGTERM lets status-observe unsubscribe and exit
    observerStopWatchdog_.start(1000);
}
void Controller::stop() {
    active_=false; restartObserver_=false;
    reconnect_.stop(); confirmation_.stop(); writeWatchdog_.stop();
    awaitingConfirmation_=false; stopObserver();
    if(writer_.state()!=QProcess::NotRunning) { writerStopping_=true; writer_.kill(); }
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
    progress_="Dauerbeobachtung wird gestartet"; ++observationStarts_;
    observer_.start(executable_, {"-H",host_,"-P",QString::number(port_),"-D",
        "--timeout",QString::number(ObserveRequestSeconds),"--idle-timeout",QString::number(ObserveIdleSeconds),
        "status-observe","-J"});
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
        hasStatus_=true; ++statusCount_; progress_="Dauerbeobachtung aktiv";
        observationWatchdog_.start(idleMs_);
        emit statusPacketReceived();
        if(!active_ || observerStopping_) return;
        // Notifications received before the write ACK cannot confirm that write.
        if(busy_ && !awaitingConfirmation_) continue;
        if(awaitingConfirmation_) { awaitingConfirmation_=false; confirmation_.stop(); setBusy(false); }
        emit statusReceived(document.object());
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
    auto reason=observerProblem_;
    if(reason.isEmpty()) {
        const auto detail=QString::fromUtf8(observerError_).trimmed().right(1500);
        reason=(code==0 && exitStatus==QProcess::NormalExit) ? "Statusbeobachtung wurde unerwartet beendet." :
            detail.isEmpty() ? "Statusbeobachtung fehlgeschlagen." : detail;
        if(!stream_.trimmed().isEmpty()) reason+="\nUnvollständige letzte Statuszeile.";
    }
    stream_.clear(); observationFailed(reason);
}
void Controller::observationFailed(const QString& reason) {
    hasStatus_=false; progress_="Warte auf Wiederverbindung";
    emit failed(reason);
    if(active_) reconnect_.start(reconnectMs_);
}
void Controller::setPower(bool on) { launchWrite({"set",on ? "pwr=1" : "pwr=0"}); }
void Controller::setHumidity(int percent) { setPanelValues({{"rhset",percent}}); }
void Controller::setPanelValues(const QJsonObject& values) {
    if(busy_ || values.isEmpty()) return;
    const bool integers=values.begin().value().isDouble();
    QStringList args{"set"}; if(integers) args<<"-I";
    for(auto i=values.begin();i!=values.end();++i) {
        const auto v=i.value(); const auto s=v.toString(); const auto n=v.toDouble(-1);
        const bool valid=
            (i.key()=="pwr" && v.isString() && (s=="0" || s=="1")) ||
            (i.key()=="cl" && v.isBool()) ||
            (i.key()=="mode" && v.isString() && QStringList{"P","A","S","M"}.contains(s)) ||
            (i.key()=="om" && v.isString() && QStringList{"1","2","3","s","t"}.contains(s)) ||
            (i.key()=="func" && v.isString() && (s=="P" || s=="PH")) ||
            (i.key()=="uil" && v.isString() && (s=="0" || s=="1")) ||
            (i.key()=="rhset" && v.isDouble() && (n==40 || n==50 || n==60 || n==70)) ||
            (i.key()=="aqil" && v.isDouble() && (n==0 || n==25 || n==50 || n==75 || n==100)) ||
            (i.key()=="dt" && v.isDouble() && n>=0 && n<=12 && n==int(n));
        if(!valid || v.isDouble()!=integers) { failCommand("Ungültiger Steuerwert: "+i.key()); return; }
        args<<i.key()+"="+(v.isBool() ? (v.toBool() ? "true" : "false") : integers ? QString::number(v.toInt()) : s);
    }
    launchWrite(args);
}
void Controller::launchWrite(const QStringList& tail) {
    if(busy_) return;
    if(writer_.state()!=QProcess::NotRunning) { failCommand("Vorheriger Schaltprozess wird noch beendet."); return; }
    const auto error=addressError();
    if(!error.isEmpty()) { failCommand(error); return; }
    if(!active_) { failCommand("Verbindungssteuerung ist nicht gestartet."); return; }
    awaitingConfirmation_=false; writerStopping_=false;
    writerError_.clear(); writerProblem_.clear(); writerBytes_=0;
    QStringList args{"-H",host_,"-P",QString::number(port_),"-D","--timeout","10","--retries","0","--no-resync"};
    args+=tail; setBusy(true);
    writer_.start(executable_,args); writeWatchdog_.start(writeMs_);
}
void Controller::writeFinished(int code, QProcess::ExitStatus exitStatus) {
    writeWatchdog_.stop();
    writerError_=(writerError_+writer_.readAllStandardError()).right(64*1024);
    writer_.readAllStandardOutput();
    if(!active_ || writerStopping_) { writerStopping_=false; return; }
    // Drain already-buffered notifications while they still cannot confirm.
    readObserver();
    if(!writerProblem_.isEmpty()) { failCommand(writerProblem_); return; }
    if(code!=0 || exitStatus!=QProcess::NormalExit) {
        const auto detail=QString::fromUtf8(writerError_).trimmed().right(1500);
        failCommand(detail.isEmpty() ? "Schaltbefehl abgelehnt oder nicht bestätigt." : detail); return;
    }
    awaitingConfirmation_=true; confirmation_.start(confirmationMs_);
    emit controlAccepted();
}
void Controller::failCommand(const QString& reason) {
    awaitingConfirmation_=false; confirmation_.stop(); setBusy(false);
    emit commandFailed(reason);
}
