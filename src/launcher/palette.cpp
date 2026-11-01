#include "palette.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace {

QVariantMap defaultColors() {
    return {
        {QStringLiteral("background"), QStringLiteral("#111318")},
        {QStringLiteral("surface"), QStringLiteral("#111318")},
        {QStringLiteral("surface_container"), QStringLiteral("#1c1f27")},
        {QStringLiteral("surface_container_high"), QStringLiteral("#262a33")},
        {QStringLiteral("on_surface"), QStringLiteral("#e6e8ee")},
        {QStringLiteral("on_surface_variant"), QStringLiteral("#9aa0ad")},
        {QStringLiteral("primary"), QStringLiteral("#7aa2ff")},
        {QStringLiteral("on_primary"), QStringLiteral("#00296b")},
        {QStringLiteral("primary_container"), QStringLiteral("#27304a")},
        {QStringLiteral("on_primary_container"), QStringLiteral("#dce6ff")},
        {QStringLiteral("error_container"), QStringLiteral("#3a2328")},
        {QStringLiteral("on_error_container"), QStringLiteral("#ffdad6")},
        {QStringLiteral("outline_variant"), QStringLiteral("#3a3f4b")},
    };
}

}

Palette::Palette(const QString& colorsSpec, const QString& wallpaperSpec, QObject* parent)
    : QObject(parent), colors_(defaultColors()), defaults_(defaultColors()) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/screenpadctl");
    configDir_ = dir;
    if (colorsSpec != QStringLiteral("none")) path_ = colorsSpec.isEmpty() ? dir + QStringLiteral("/colors.json") : colorsSpec;
    wallpaperPath_ = wallpaperSpec.isEmpty() ? dir + QStringLiteral("/wallpaper") : wallpaperSpec;
    retry_.setSingleShot(true);
    retry_.setInterval(200);
    connect(&retry_, &QTimer::timeout, this, &Palette::reload);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &Palette::onFileChanged);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &Palette::onFileChanged);
    reload();
}

void Palette::reload() {
    QVariantMap colors = colors_;
    QFile file(path_);
    if (file.open(QIODevice::ReadOnly)) {
        colors = defaultColors();
        const QJsonObject overrides = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
            if (it.value().isString()) colors.insert(it.key(), it.value().toString());
    }

    const QString wallpaper = wallpaperPath_ != QStringLiteral("none") && QFileInfo(wallpaperPath_).isFile()
        ? QUrl::fromLocalFile(QFileInfo(wallpaperPath_).canonicalFilePath()).toString() : QString();

    if (colors != colors_ || wallpaper != wallpaper_) {
        colors_ = colors;
        wallpaper_ = wallpaper;
        emit changed();
    }
    for (const QString& watched : {configDir_, path_, wallpaperPath_, QFileInfo(wallpaperPath_).canonicalFilePath()})
        if (!watched.isEmpty() && QFileInfo::exists(watched) && !watcher_.files().contains(watched) && !watcher_.directories().contains(watched))
            watcher_.addPath(watched);
}

void Palette::onFileChanged() {
    reload();
    if (!QFileInfo::exists(path_) || !QFileInfo::exists(wallpaperPath_)) retry_.start();
}
