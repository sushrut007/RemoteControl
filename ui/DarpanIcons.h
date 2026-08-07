#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

class DarpanIcons
{
public:
    static QIcon icon(const QString& name,
        const QSize& size = QSize(20, 20),
        const QColor& color = QColor());

    static QPixmap tintedPixmap(const QString& name,
        const QColor& color,
        const QSize& size = QSize(20, 20));

    static bool isAvailable(const QString& name);
};
