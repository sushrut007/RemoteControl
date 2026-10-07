#pragma once

#include <QSize>

namespace darpan::platform {

class InputInjector
{
public:
    void setStreamSize(const QSize &size);
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }

    void handleControlMessage(uint8_t type, const QByteArray &body);

private:
    void injectMouseMove(uint16_t nx, uint16_t ny);
    void injectMouseButton(uint8_t button, bool down, uint16_t nx, uint16_t ny);
    void injectWheel(int16_t delta, uint16_t nx, uint16_t ny);
    void injectKey(bool down, uint16_t vk, bool extended);

    QSize m_streamSize = QSize(1920, 1080);
    bool m_enabled = false;
};

} // namespace darpan::platform
