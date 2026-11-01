#include "uisettings.h"

#include <algorithm>

UiSettings::UiSettings(QObject* parent)
    : QObject(parent), settings_(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("screenpadctl"), QStringLiteral("launcher")) {
    setShowWallpaper(settings_.value(QStringLiteral("showWallpaper"), showWallpaper_).toBool());
    setWallpaperDim(settings_.value(QStringLiteral("wallpaperDim"), wallpaperDim_).toDouble());
    setFollowShellColors(settings_.value(QStringLiteral("followShellColors"), followShellColors_).toBool());
    setCardOpacity(settings_.value(QStringLiteral("cardOpacity"), cardOpacity_).toDouble());
    setCardRadius(settings_.value(QStringLiteral("cardRadius"), cardRadius_).toInt());
    setIconSize(settings_.value(QStringLiteral("iconSize"), iconSize_).toInt());
    setClock24h(settings_.value(QStringLiteral("clock24h"), clock24h_).toBool());
    setShowDate(settings_.value(QStringLiteral("showDate"), showDate_).toBool());
    setClockSize(settings_.value(QStringLiteral("clockSize"), clockSize_).toInt());
}

template <typename T>
bool UiSettings::store(T& field, const QString& key, const T& value) {
    if (field == value) return false;
    field = value;
    settings_.setValue(key, value);
    settings_.sync();
    return true;
}

void UiSettings::setShowWallpaper(bool value) {
    if (store(showWallpaper_, QStringLiteral("showWallpaper"), value)) emit showWallpaperChanged();
}

void UiSettings::setWallpaperDim(double value) {
    if (store(wallpaperDim_, QStringLiteral("wallpaperDim"), std::clamp(value, 0.0, 0.9))) emit wallpaperDimChanged();
}

void UiSettings::setFollowShellColors(bool value) {
    if (store(followShellColors_, QStringLiteral("followShellColors"), value)) emit followShellColorsChanged();
}

void UiSettings::setCardOpacity(double value) {
    if (store(cardOpacity_, QStringLiteral("cardOpacity"), std::clamp(value, 0.2, 1.0))) emit cardOpacityChanged();
}

void UiSettings::setCardRadius(int value) {
    if (store(cardRadius_, QStringLiteral("cardRadius"), std::clamp(value, 0, 28))) emit cardRadiusChanged();
}

void UiSettings::setIconSize(int value) {
    if (store(iconSize_, QStringLiteral("iconSize"), std::clamp(value, 40, 80))) emit iconSizeChanged();
}

void UiSettings::setClock24h(bool value) {
    if (store(clock24h_, QStringLiteral("clock24h"), value)) emit clock24hChanged();
}

void UiSettings::setShowDate(bool value) {
    if (store(showDate_, QStringLiteral("showDate"), value)) emit showDateChanged();
}

void UiSettings::setClockSize(int value) {
    if (store(clockSize_, QStringLiteral("clockSize"), std::clamp(value, 28, 64))) emit clockSizeChanged();
}
