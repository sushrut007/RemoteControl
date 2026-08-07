#include "DarpanStyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>
#include <QDebug>

namespace {

QString readStyleSheet()
{
    static const QStringList paths = {
        QStringLiteral(":/styles/darpan.qss"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/styles/darpan.qss"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../styles/darpan.qss"),
    };

    for (const QString& path : paths) {
        QFile file(path);
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            return QString::fromUtf8(file.readAll());
        }
    }
    return {};
}

void applyDarkPalette(QApplication* app)
{
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x0B, 0x0E, 0x14));
    pal.setColor(QPalette::WindowText, QColor(0xF0, 0xF6, 0xFC));
    pal.setColor(QPalette::Base, QColor(0x0B, 0x0E, 0x14));
    pal.setColor(QPalette::AlternateBase, QColor(0x19, 0x1C, 0x22));
    pal.setColor(QPalette::Text, QColor(0xF0, 0xF6, 0xFC));
    pal.setColor(QPalette::Button, QColor(0x21, 0x26, 0x2D));
    pal.setColor(QPalette::ButtonText, QColor(0xF0, 0xF6, 0xFC));
    pal.setColor(QPalette::Highlight, QColor(0x63, 0x66, 0xF1));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x21, 0x26, 0x2D));
    pal.setColor(QPalette::ToolTipText, QColor(0xF0, 0xF6, 0xFC));
    app->setPalette(pal);
}

} // namespace

QString DarpanStyle::readStylesheet()
{
    return readStyleSheet();
}

bool DarpanStyle::apply(QApplication* app)
{
    if (!app) {
        return false;
    }

    // Native Windows styles ignore large parts of QSS; Fusion respects it.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app->setStyle(fusion);
    }

    applyDarkPalette(app);

    const QString sheet = readStyleSheet();
    if (sheet.isEmpty()) {
        qWarning() << "[DarpanStyle] Could not load darpan.qss from Qt resources or disk.";
        return false;
    }

    app->setStyleSheet(sheet);
    return true;
}
