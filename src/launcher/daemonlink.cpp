#include "daemonlink.h"

#include <QDebug>
#include <QRegularExpression>

namespace {
QString socketPath() {
    const QByteArray p = qgetenv("SCREENPADCTL_SOCKET");
    return p.isEmpty() ? QStringLiteral("/run/screenpadctl.sock") : QString::fromUtf8(p);
}

template <typename T>
bool assignIfChanged(T& dst, const T& value) {
    if (dst == value) return false;
    dst = value;
    return true;
}
}

DaemonLink::DaemonLink(QObject* parent) : QObject(parent) {
    ping_.setInterval(2000);
    connect(&ping_, &QTimer::timeout, this, [this] { sendLine("ping"); });
    connect(&sock_, &QLocalSocket::readyRead, this, &DaemonLink::onReadyRead);
    connect(&sock_, &QLocalSocket::connected, this, [this] { setState(Touching); sendLine("touch"); });
    retry_.setSingleShot(true);
    retry_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, &DaemonLink::start);
    connect(&sock_, &QLocalSocket::errorOccurred, this, [this] {
        if (state_ != Gone) dropLink(QString("daemon unreachable: %1").arg(sock_.errorString()));
    });
    connect(&sock_, &QLocalSocket::disconnected, this, [this] {
        if (state_ != Gone) dropLink(QStringLiteral("connection to the daemon lost"));
    });
}

void DaemonLink::setState(State s) {
    const bool wasReady = ready();
    state_ = s;
    if (s != Ready) ping_.stop();
    if (wasReady != ready()) emit readyChanged();
}

void DaemonLink::dropLink(const QString& why) {
    if (!everReady_) { setState(Gone); emit lost(why); return; }
    const bool wasReady = ready();
    setState(Connecting);
    if (wasReady) emit closeRequested();
    sock_.abort();
    retry_.start();
}

void DaemonLink::start() {
    setState(Connecting);
    sock_.connectToServer(socketPath());
}

void DaemonLink::sendLine(const QString& line) {
    if (sock_.state() == QLocalSocket::ConnectedState) sock_.write((line + "\n").toUtf8());
}

void DaemonLink::shutdown() {
    if (state_ == Gone) return;
    const bool wasConnected = sock_.state() == QLocalSocket::ConnectedState;
    setState(Gone);
    if (wasConnected) {
        sock_.write("trackpad off\npointer\n");
        sock_.waitForBytesWritten(300);
    }
    sock_.disconnectFromServer();
}

void DaemonLink::trackpad(bool on, double x0, double y0, double x1, double y1) {
    if (!ready()) return;
    if (on) sendLine(QString("trackpad on %1 %2 %3 %4").arg(x0, 0, 'f', 4).arg(y0, 0, 'f', 4).arg(x1, 0, 'f', 4).arg(y1, 0, 'f', 4));
    else sendLine("trackpad off");
}

void DaemonLink::setBrightness(int value) {
    if (ready()) sendLine(QString("brightness %1").arg(value));
}

void DaemonLink::applySettings(const QString& line) {
    bool changed = false;
    for (const QString& field : line.split(' ', Qt::SkipEmptyParts)) {
        const qsizetype eq = field.indexOf('=');
        if (eq < 0) continue;
        const QString key = field.left(eq);
        const QString value = field.mid(eq + 1);
        if (key == "brightness") changed |= assignIfChanged(brightness_, value.toInt());
    }
    if (changed) emit settingsChanged();
}

void DaemonLink::onReadyRead() {
    buf_ += sock_.readAll();
    qsizetype nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        handleLine(QString::fromUtf8(buf_.left(nl)));
        buf_.remove(0, nl + 1);
    }
}

void DaemonLink::handleLine(const QString& line) {
    if (line.startsWith("event ")) {
        if (line == "event close") emit closeRequested();
        return;
    }
    if (state_ == Touching) {
        if (line.startsWith("ok")) {
            static const QRegularExpression re("hypr=(\\S+)");
            const auto m = re.match(line);
            everReady_ = true;
            setState(Ready);
            ping_.start();
            applySettings(line);
            emit touchReady(m.hasMatch() ? m.captured(1) : QString());
        } else {
            setState(Gone);
            emit lost(line.startsWith("err ") ? line.mid(4) : line);
        }
        return;
    }
    if (state_ == Ready) {
        if (line.startsWith("err")) emit error(line.mid(3).trimmed());
        else if (line.contains("mode=pointer")) { setState(Gone); emit lost(QStringLiteral("daemon left touch mode")); }
        else if (line.startsWith("ok")) applySettings(line);
    }
}
