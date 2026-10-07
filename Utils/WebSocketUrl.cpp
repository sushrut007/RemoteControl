#include "WebSocketUrl.h"

#include <QUrl>

namespace darpan::util {

QString normalizeSignalingWebSocketUrl(const QString &url)
{
    QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("ws://127.0.0.1:8765/ws");
    }

    QUrl parsed(trimmed);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        if (!trimmed.startsWith(QStringLiteral("ws"), Qt::CaseInsensitive)) {
            trimmed.prepend(QStringLiteral("ws://"));
        }
        parsed = QUrl(trimmed);
    }

    QString path = parsed.path();
    if (path.isEmpty() || path == QStringLiteral("/")) {
        parsed.setPath(QStringLiteral("/ws"));
    } else if (!path.endsWith(QStringLiteral("/ws"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path.append(QLatin1Char('/'));
        }
        path.append(QStringLiteral("ws"));
        parsed.setPath(path);
    }

    return parsed.toString();
}

} // namespace darpan::util
