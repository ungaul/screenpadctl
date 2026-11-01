#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class Palette : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantMap defaults READ defaults CONSTANT)
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY changed)
public:
    Palette(const QString& colorsSpec, const QString& wallpaperSpec, QObject* parent = nullptr);

    QVariantMap colors() const { return colors_; }
    QVariantMap defaults() const { return defaults_; }
    QString wallpaper() const { return wallpaper_; }

signals:
    void changed();

private:
    void reload();
    void onFileChanged();

    QString path_;
    QString configDir_;
    QString wallpaperPath_;
    QVariantMap colors_;
    QVariantMap defaults_;
    QString wallpaper_;
    QFileSystemWatcher watcher_;
    QTimer retry_;
};
