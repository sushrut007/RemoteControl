#include "darpan.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Darpan"));
    QApplication::setOrganizationName(QStringLiteral("Darpan"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    DarpanMainWindow window;
    window.show();

    return app.exec();
}
