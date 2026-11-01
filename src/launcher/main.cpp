#include <QCommandLineParser>
#include <QDebug>
#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include "daemonlink.h"
#include "desktopmodel.h"
#include "hyprland.h"
#include "palette.h"
#include "uisettings.h"

namespace {

class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& req) override {
        const QSize s = req.isValid() ? req : QSize(128, 128);
        QIcon ic = QIcon::fromTheme(id);
        if (ic.isNull()) ic = QIcon::fromTheme("application-x-executable");
        QPixmap px = ic.pixmap(s);
        if (px.isNull()) px = QIcon::fromTheme("application-x-executable").pixmap(s);
        if (size) *size = px.size();
        return px;
    }
};

QScreen* findPad(const QString& wanted) {
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens)
        if (!wanted.isEmpty() && s->name() == wanted) return s;
    for (QScreen* s : screens)
        if (s->model().contains("ScreenXpert", Qt::CaseInsensitive)) return s;
    for (QScreen* s : screens) {
        const QSize px = s->size() * s->devicePixelRatio();
        if (s != QGuiApplication::primaryScreen() && (px.width() * 2 == px.height() || px.height() * 2 == px.width())) return s;
    }
    return nullptr;
}

}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("screenpadctl");
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("hicolor");

    QCommandLineParser cli;
    cli.addHelpOption();
    QCommandLineOption screenOpt("screen", "Name of the ScreenPad output (e.g. HDMI-A-1)", "name");
    QCommandLineOption targetOpt("target", "Output where applications open (default: last active output other than the ScreenPad)", "name");
    QCommandLineOption windowedOpt("windowed", "Plain window instead of fullscreen");
    QCommandLineOption noDaemonOpt("no-daemon", "Do not contact screenpadctld (development, no touch)");
    QCommandLineOption colorsOpt("colors", "JSON of Material roles to color the launcher, or 'none' for built-in colors (default: ~/.config/screenpadctl/colors.json)", "path|none");
    QCommandLineOption wallpaperOpt("wallpaper", "Image to show behind the launcher, or 'none' (default: ~/.config/screenpadctl/wallpaper)", "path|none");
    cli.addOptions({screenOpt, targetOpt, windowedOpt, noDaemonOpt, colorsOpt, wallpaperOpt});
    cli.process(app);

    QScreen* pad = findPad(cli.value(screenOpt));
    if (!pad) {
        qCritical("ScreenPad not found. Available outputs:");
        for (QScreen* s : QGuiApplication::screens()) qCritical() << " " << s->name() << s->model();
        qCritical("Pass it with --screen <name>.");
        return 1;
    }

    const QStringList dirs{QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)};

    QString target = cli.value(targetOpt);
    if (target.isEmpty()) {
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary && primary != pad) target = primary->name();
        else for (QScreen* s : QGuiApplication::screens()) if (s != pad) { target = s->name(); break; }
    }

    DesktopModel model(dirs);
    Hyprland hypr(pad->name(), target);
    model.setHyprland(&hypr);
    hypr.configurePad();
    if (pad->size().width() < pad->size().height()) {
        QEventLoop wait;
        QTimer::singleShot(2000, &wait, &QEventLoop::quit);
        QObject::connect(pad, &QScreen::geometryChanged, &wait, [&] { if (pad->size().width() > pad->size().height()) wait.quit(); });
        wait.exec();
    }
    DaemonLink link;
    Palette palette(cli.value(colorsOpt), cli.value(wallpaperOpt));
    UiSettings ui;

    if (const QString family = ui.fontFamily(); !family.isEmpty()) QGuiApplication::setFont(QFont(family));

    QQmlApplicationEngine engine;
    engine.addImageProvider("icon", new IconProvider);
    engine.rootContext()->setContextProperty("shortcuts", &model);
    engine.rootContext()->setContextProperty("daemon", &link);
    engine.rootContext()->setContextProperty("theme", &palette);
    engine.rootContext()->setContextProperty("ui", &ui);
    engine.loadFromModule("Screenpadctl", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    win->setScreen(pad);
    if (cli.isSet(windowedOpt)) { win->resize(pad->size()); win->show(); }
    else win->showFullScreen();

    QObject::connect(&hypr, &Hyprland::padReturned, &app, &QGuiApplication::quit);

    if (!cli.isSet(noDaemonOpt)) {

        QString touchDev;
        QObject::connect(&link, &DaemonLink::touchReady, &app, [&](const QString& dev) {
            touchDev = dev;
            QTimer::singleShot(700, &app, [&, dev] { hypr.mapTouch(dev, pad->name()); });
        });

        QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, [&] { hypr.mapTouch(touchDev, QString()); });
        QObject::connect(&link, &DaemonLink::lost, &app, [&](const QString& why) { qWarning() << "screenpadctl :" << why; app.quit(); });
        QObject::connect(&app, &QGuiApplication::aboutToQuit, &link, &DaemonLink::shutdown);
        link.start();
    }
    return app.exec();
}
