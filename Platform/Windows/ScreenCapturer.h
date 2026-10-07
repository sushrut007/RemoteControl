#pragma once

#include <QImage>
#include <QObject>
#include <QSize>

namespace darpan::platform {

class ScreenCapturer : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCapturer(QObject *parent = nullptr);
    ~ScreenCapturer() override;

    void start(int intervalMs = 66);
    void stop();
    bool isActive() const { return m_active; }

    QString captureBackend() const { return m_backend; }

signals:
    void frameCaptured(const QImage &frame, const QSize &streamSize);

private:
    QImage capturePrimaryMonitor(QSize *streamSizeOut);

    bool m_active = false;
    mutable bool m_dxgiRejected = false;
    QString m_backend = QStringLiteral("qt");
    class QTimer *m_timer = nullptr;
};

} // namespace darpan::platform
