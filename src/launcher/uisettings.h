#pragma once
#include <QObject>
#include <QSettings>

class UiSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool showWallpaper READ showWallpaper WRITE setShowWallpaper NOTIFY showWallpaperChanged)
    Q_PROPERTY(double wallpaperDim READ wallpaperDim WRITE setWallpaperDim NOTIFY wallpaperDimChanged)
    Q_PROPERTY(bool followShellColors READ followShellColors WRITE setFollowShellColors NOTIFY followShellColorsChanged)
    Q_PROPERTY(double cardOpacity READ cardOpacity WRITE setCardOpacity NOTIFY cardOpacityChanged)
    Q_PROPERTY(int cardRadius READ cardRadius WRITE setCardRadius NOTIFY cardRadiusChanged)
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(bool clock24h READ clock24h WRITE setClock24h NOTIFY clock24hChanged)
    Q_PROPERTY(bool showDate READ showDate WRITE setShowDate NOTIFY showDateChanged)
    Q_PROPERTY(int clockSize READ clockSize WRITE setClockSize NOTIFY clockSizeChanged)

public:
    explicit UiSettings(QObject* parent = nullptr);

    bool showWallpaper() const { return showWallpaper_; }
    double wallpaperDim() const { return wallpaperDim_; }
    bool followShellColors() const { return followShellColors_; }
    double cardOpacity() const { return cardOpacity_; }
    int cardRadius() const { return cardRadius_; }
    int iconSize() const { return iconSize_; }
    bool clock24h() const { return clock24h_; }
    QString fontFamily() const { return settings_.value(QStringLiteral("fontFamily")).toString(); }
    bool showDate() const { return showDate_; }
    int clockSize() const { return clockSize_; }

    void setShowWallpaper(bool value);
    void setWallpaperDim(double value);
    void setFollowShellColors(bool value);
    void setCardOpacity(double value);
    void setCardRadius(int value);
    void setIconSize(int value);
    void setClock24h(bool value);
    void setShowDate(bool value);
    void setClockSize(int value);

signals:
    void showWallpaperChanged();
    void wallpaperDimChanged();
    void followShellColorsChanged();
    void cardOpacityChanged();
    void cardRadiusChanged();
    void iconSizeChanged();
    void clock24hChanged();
    void showDateChanged();
    void clockSizeChanged();

private:
    template <typename T>
    bool store(T& field, const QString& key, const T& value);

    QSettings settings_;
    bool showWallpaper_ = true;
    double wallpaperDim_ = 0.45;
    bool followShellColors_ = true;
    double cardOpacity_ = 0.72;
    int cardRadius_ = 16;
    int iconSize_ = 56;
    bool clock24h_ = true;
    bool showDate_ = true;
    int clockSize_ = 40;
};
