#pragma once
#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QString>

class Hyprland : public QObject {
    Q_OBJECT
public:
    Hyprland(QString padName, QString fallbackTarget, QObject* parent = nullptr);

    bool active() const { return active_; }
    QString targetMonitor() const { return last_.isEmpty() ? fallback_ : last_; }

    bool launch(const QString& cmd, const QString& workDir);

    void mapTouch(const QString& device, const QString& output);

    void configurePad();

signals:
    void padReturned();

private:
    void readEvents();
    void handleEvent(const QString& line);
    bool focus(const QString& monitor);

    QLocalSocket sock_;
    QByteArray buf_;
    QString pad_, fallback_, last_;
    bool active_ = false;
    bool padRemoved_ = false;
};
