#pragma once
#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class DaemonLink : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(int brightness READ brightness NOTIFY settingsChanged)
public:
    explicit DaemonLink(QObject* parent = nullptr);

    bool ready() const { return state_ == Ready; }
    int brightness() const { return brightness_; }
    void start();
    void shutdown();

    Q_INVOKABLE void trackpad(bool on, double x0 = 0, double y0 = 0, double x1 = 0, double y1 = 0);
    Q_INVOKABLE void setBrightness(int value);

signals:
    void readyChanged();
    void settingsChanged();
    void touchReady(const QString& hyprDevice);
    void closeRequested();
    void error(const QString& message);
    void lost(const QString& reason);

private:
    void dropLink(const QString& why);
    enum State { Idle, Connecting, Touching, Ready, Gone };
    void onReadyRead();
    void handleLine(const QString& line);
    void applySettings(const QString& line);
    void sendLine(const QString& line);
    void setState(State s);

    QLocalSocket sock_;
    QTimer ping_;
    QTimer retry_;
    bool everReady_ = false;
    QByteArray buf_;
    State state_ = Idle;
    int brightness_ = 0;
};
