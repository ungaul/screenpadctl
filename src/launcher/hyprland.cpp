#include "hyprland.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

namespace {

QString luaStr(const QString& s) { return "[==[" + s + "]==]"; }
QString shQuote(const QString& s) { return "'" + QString(s).replace("'", "'\\''") + "'"; }

QString hyprctl(const QStringList& args, int timeoutMs = 1500) {
    QProcess p;
    p.start("hyprctl", args);
    if (!p.waitForFinished(timeoutMs)) { p.kill(); return {}; }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
bool hyprctlOk(const QStringList& args) { return hyprctl(args) == "ok"; }

}

Hyprland::Hyprland(QString padName, QString fallbackTarget, QObject* parent)
    : QObject(parent), pad_(std::move(padName)), fallback_(std::move(fallbackTarget)) {
    const QByteArray sig = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    if (sig.isEmpty() || runtime.isEmpty()) return;
    active_ = true;

    QString name;
    for (const QString& line : hyprctl({"monitors"}).split('\n')) {
        if (line.startsWith("Monitor ")) name = line.section(' ', 1, 1);
        else if (line.trimmed() == "focused: yes" && name != pad_) { last_ = name; break; }
    }

    connect(&sock_, &QLocalSocket::readyRead, this, &Hyprland::readEvents);
    sock_.connectToServer(QString("%1/hypr/%2/.socket2.sock").arg(QString::fromUtf8(runtime), QString::fromUtf8(sig)));
}

void Hyprland::readEvents() {
    buf_ += sock_.readAll();
    qsizetype nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        handleEvent(QString::fromUtf8(buf_.left(nl)));
        buf_.remove(0, nl + 1);
    }
}

void Hyprland::handleEvent(const QString& line) {
    const qsizetype sep = line.indexOf(">>");
    if (sep < 0) return;
    const QString ev = line.left(sep);
    if (ev != "focusedmon" && ev != "focusedmonv2") return;
    const QString mon = line.mid(sep + 2).section(',', 0, 0);
    if (!mon.isEmpty() && mon != pad_) last_ = mon;
}

bool Hyprland::focus(const QString& monitor) {
    return hyprctlOk({"eval", "hl.dispatch(hl.dsp.focus({ monitor = " + luaStr(monitor) + " }))"})
        || hyprctlOk({"dispatch", "focusmonitor", monitor});
}

bool Hyprland::launch(const QString& cmd, const QString& workDir) {
    const QString target = targetMonitor();
    if (!active_ || target.isEmpty()) return false;
    focus(target);
    const QString full = workDir.isEmpty() ? cmd : "cd " + shQuote(workDir) + " && " + cmd;
    return hyprctlOk({"eval", "hl.exec_cmd(" + luaStr(full) + ", { monitor = " + luaStr(target) + " })"})
        || hyprctlOk({"dispatch", "exec", "[monitor " + target + "] " + full});
}

void Hyprland::mapTouch(const QString& device, const QString& output) {

    if (!active_ || device.isEmpty()) return;
    hyprctlOk({"eval", "hl.device({ name = " + luaStr(device) + ", output = " + luaStr(output) + " })"})
        || hyprctlOk({"keyword", "device[" + device + "]:output", output});
}

void Hyprland::configurePad() {
    if (!active_) return;
    const QJsonArray monitors = QJsonDocument::fromJson(hyprctl({"-j", "monitors"}).toUtf8()).array();
    for (const QJsonValue& value : monitors) {
        const QJsonObject m = value.toObject();
        if (m.value("name").toString() != pad_) continue;
        if (m.value("transform").toInt() != 3 || m.value("scale").toDouble() != 2.0) {
            const QString mode = QString("%1x%2@%3").arg(m.value("width").toInt()).arg(m.value("height").toInt()).arg(m.value("refreshRate").toDouble(), 0, 'f', 2);
            const QString position = QString("%1x%2").arg(m.value("x").toInt()).arg(m.value("y").toInt());
            hyprctlOk({"eval", "hl.monitor({ output = " + luaStr(pad_) + ", mode = " + luaStr(mode) + ", position = " + luaStr(position) + ", scale = 2, transform = 3 })"});
        }
        hyprctlOk({"eval", "hl.window_rule({ name = \"screenpadctl-launcher\", match = { class = \"^screenpadctl-launcher$\" }, monitor = " + luaStr(pad_) + ", float = false, fullscreen = true })"});
        hyprctlOk({"eval", "hl.workspace_rule({ workspace = \"name:screenpad\", monitor = " + luaStr(pad_) + ", default = true, persistent = true })"});
        return;
    }
}
