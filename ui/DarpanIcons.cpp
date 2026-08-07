#include "DarpanIcons.h"
#include "DarpanTheme.h"

#include <QFile>
#include <QPainter>
#include <QIcon>
#include <QImage>
#include <QSvgRenderer>
#include <QCoreApplication>
#include <QDebug>

namespace {

QString iconResourcePath(const QString& name)
{
    return QStringLiteral(":/assets/icons/%1.svg").arg(name);
}

QString iconDiskPath(const QString& name)
{
    return QCoreApplication::applicationDirPath()
        + QStringLiteral("/assets/icons/%1.svg").arg(name);
}

bool loadSvg(QSvgRenderer& renderer, const QString& name)
{
    const QString resourcePath = iconResourcePath(name);
    if (QFile::exists(resourcePath)) {
        renderer.load(resourcePath);
        if (renderer.isValid()) {
            return true;
        }
    }

    const QString diskPath = iconDiskPath(name);
    if (QFile::exists(diskPath)) {
        renderer.load(diskPath);
        if (renderer.isValid()) {
            return true;
        }
    }

    qWarning() << "[DarpanIcons] Failed to load icon:" << name;
    return false;
}

QPixmap renderSvg(const QString& name, const QSize& size, const QColor& tint)
{
    QSvgRenderer renderer;
    if (!loadSvg(renderer, name)) {
        return QPixmap();
    }

    const QSize renderSize = size.isValid() ? size : QSize(20, 20);
    // Render at 2x for sharper downscale on HiDPI displays.
    const int scale = 2;
    const QSize hiRes(renderSize.width() * scale, renderSize.height() * scale);

    QImage img(hiRes, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(QPointF(0, 0), hiRes));
    painter.end();

    if (tint.isValid()) {
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int alpha = qAlpha(line[x]);
                if (alpha > 0) {
                    line[x] = qRgba(tint.red(), tint.green(), tint.blue(), alpha);
                }
            }
        }
    }

    return QPixmap::fromImage(
        img.scaled(renderSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace

QIcon DarpanIcons::icon(const QString& name, const QSize& size, const QColor& color)
{
    const QColor tint = color.isValid()
        ? color
        : QColor(DarpanTheme::kTextSecondary);

    QIcon ico;
    const QPixmap normal = renderSvg(name, size, tint);
    if (normal.isNull()) {
        return ico;
    }

    ico.addPixmap(normal, QIcon::Normal, QIcon::Off);
    ico.addPixmap(normal, QIcon::Disabled, QIcon::Off);

    const QPixmap hover = renderSvg(name, size, QColor(DarpanTheme::kTextPrimary));
    ico.addPixmap(hover, QIcon::Active, QIcon::Off);

    return ico;
}

QPixmap DarpanIcons::tintedPixmap(const QString& name, const QColor& color, const QSize& size)
{
    return renderSvg(name, size, color);
}

bool DarpanIcons::isAvailable(const QString& name)
{
    QSvgRenderer renderer;
    return loadSvg(renderer, name);
}
