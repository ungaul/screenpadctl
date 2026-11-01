#include "desktopmodel.h"
#include "hyprland.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <algorithm>

namespace {
QString themeIcon(const QString& name) { return "image://icon/" + name; }
}

DesktopModel::DesktopModel(QStringList dirs, QObject* parent)
    : QAbstractListModel(parent), dirs_(std::move(dirs)) { reload(); }

QHash<int, QByteArray> DesktopModel::roleNames() const {
    return {{NameRole, "name"}, {IconRole, "icon"}, {KindRole, "kind"}};
}

QVariant DesktopModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rowCount()) return {};
    const Item& it = items_[idx.row()];
    switch (role) {
        case NameRole: return it.name;
        case IconRole: return it.icon;
        case KindRole: return it.kind;
    }
    return {};
}

QString DesktopModel::stripFieldCodes(const QString& exec) {
    QString out;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%') out += '%';
            ++i;
        } else {
            out += exec[i];
        }
    }
    return out.simplified();
}

bool DesktopModel::parseDesktop(const QString& path, Item& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    const QString lang = QLocale().name().section('_', 0, 0);
    bool inEntry = false, hidden = false;
    QString name, localName, icon, exec, path_, type;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith('[')) { inEntry = (line == "[Desktop Entry]"); continue; }
        if (!inEntry || line.startsWith('#')) continue;
        const qsizetype eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString k = line.left(eq), v = line.mid(eq + 1);
        if (k == "Name") name = v;
        else if (k == "Name[" + lang + "]") localName = v;
        else if (k == "Icon") icon = v;
        else if (k == "Exec") exec = v;
        else if (k == "Path") path_ = v;
        else if (k == "Type") type = v;
        else if ((k == "NoDisplay" || k == "Hidden") && v == "true") hidden = true;
    }
    if (hidden || type != "Application" || exec.isEmpty()) return false;
    out.name = localName.isEmpty() ? name : localName;
    out.icon = icon.startsWith('/') ? "file://" + icon : themeIcon(icon.isEmpty() ? "application-x-executable" : icon);
    out.kind = "app";
    out.target = path;
    out.exec = stripFieldCodes(exec);
    out.workDir = path_;
    return !out.name.isEmpty();
}

void DesktopModel::reload() {
    beginResetModel();
    items_.clear();
    for (const QString& d : std::as_const(dirs_)) {
        const QDir dir(d);
        if (!dir.exists()) continue;
        const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                               QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& fi : entries) {
            if (fi.fileName().startsWith('.')) continue;
            Item it;
            if (fi.isDir()) {
                it = {fi.fileName(), themeIcon("folder"), "folder", fi.absoluteFilePath(), {}, {}};
            } else if (fi.suffix() == "desktop") {
                if (!parseDesktop(fi.absoluteFilePath(), it)) continue;
            } else {
                continue;
            }
            items_.push_back(std::move(it));
        }
    }
    endResetModel();
}

namespace {
QString shQuote(const QString& s) { return "'" + QString(s).replace("'", "'\\''") + "'"; }
}

void DesktopModel::run(const QString& cmd, const QString& workDir) const {
    if (hypr_ && hypr_->launch(cmd, workDir)) return;
    QProcess p;
    p.setProgram("/bin/sh");
    p.setArguments({"-c", "exec " + cmd});
    if (!workDir.isEmpty()) p.setWorkingDirectory(workDir);
    p.startDetached();
}

void DesktopModel::launch(int row) {
    if (row < 0 || row >= rowCount()) return;
    const Item& it = items_[row];
    if (it.kind == "app") run(it.exec, it.workDir);
    else run("xdg-open " + shQuote(it.target), {});
}
