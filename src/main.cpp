// chalk — a simple whiteboard for diagrams and rough layouts.

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QUrl>

#include "backend.h"
#include "canvasview.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands, exports and help draw no window, so they must not need a
    // display. Bare chalk prints help, as a command line tool should; the
    // launcher says chalk open.
    const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool command = argc == 1 || isCliCommand(first) || first == "--help" || first == "-h"
                         || first == "--version" || first == "-v";
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("chalk");
    app.setApplicationVersion("0.3.0");
    if (command) {
        if (first == "--version" || first == "-v") {
            printf("chalk %s\n", qPrintable(app.applicationVersion()));
            return 0;
        }
        return runCli(app.arguments());
    }

    // Matches the window to chalk.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("chalk");
    app.setWindowIcon(QIcon::fromTheme("chalk"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);

    // Light or dark follows the Omarchy theme until it's been picked by
    // hand; after that the pick is remembered.
    QSettings settings("chalk", "chalk");
    if (settings.contains("appearance"))
        theme.setDark(settings.value("appearance").toString() != "light");
    QObject::connect(&theme, &Theme::darkChosen, &app, [&settings](bool dark) {
        settings.setValue("appearance", dark ? "dark" : "light");
    });

    // chalk open board.chalk, or just chalk board.chalk. With no board named
    // it picks up the one that was open last, if it's still there.
    QStringList arguments = app.arguments().mid(1);
    if (arguments.value(0) == "open")
        arguments.removeFirst();
    const QString named = arguments.value(0);
    const QString last = settings.value("lastBoard").toString();
    // Connected before the first load, so a board named on the command
    // line is remembered as the last one too.
    QObject::connect(&backend, &Backend::fileChanged, &app, [&] {
        settings.setValue("lastBoard", backend.filePath());
    });
    if (!named.isEmpty())
        backend.load(named);
    else if (!last.isEmpty() && QFileInfo::exists(last))
        backend.load(last);

    qmlRegisterType<CanvasView>("Chalk", 1, 0, "CanvasView");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
