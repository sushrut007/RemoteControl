#include "RemoteDeviceControl.h"
#include "ui/AppShell.h"
#include <QApplication>
#include <QScreen>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RemoteControl"));
    app.setOrganizationName(QStringLiteral("SushrutMakes"));
    app.setOrganizationDomain(QStringLiteral("SushrutMakes.local"));

    // High-DPI already enabled by default in Qt 6;
    // uncomment the line below for Qt 5:
    // QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    RemoteDeviceControl bootstrap;
    bootstrap.launch();

    // Center window on primary screen
    if (QScreen* screen = app.primaryScreen()) {
        const QRect sg = screen->availableGeometry();
        bootstrap.shell()->move(
            sg.center() - bootstrap.shell()->rect().center());
    }

    return app.exec();
}
