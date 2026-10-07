#include "InputInjector.h"

#include "RemoteSession/ControlProtocol.h"

#include <QByteArray>
#include <QSize>

#include <QtGlobal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace darpan::platform {

namespace {

uint16_t readU16(const QByteArray &body, int offset)
{
    return uint16_t(uint8_t(body[offset])) | (uint16_t(uint8_t(body[offset + 1])) << 8);
}

int16_t readI16(const QByteArray &body, int offset)
{
    return int16_t(readU16(body, offset));
}

} // namespace

void InputInjector::setStreamSize(const QSize &size)
{
    if (size.width() > 0 && size.height() > 0) {
        m_streamSize = size;
    }
}

void InputInjector::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void InputInjector::handleControlMessage(uint8_t type, const QByteArray &body)
{
    if (!m_enabled) {
        return;
    }
    switch (type) {
    case protocol::kMouseMove:
        if (body.size() >= 4) {
            injectMouseMove(readU16(body, 0), readU16(body, 2));
        }
        break;
    case protocol::kMouseButton:
        if (body.size() >= 6) {
            injectMouseButton(uint8_t(body[0]), body[1] != 0, readU16(body, 2), readU16(body, 4));
        }
        break;
    case protocol::kMouseWheel:
        if (body.size() >= 6) {
            injectWheel(readI16(body, 0), readU16(body, 2), readU16(body, 4));
        }
        break;
    case protocol::kKeyDown:
    case protocol::kKeyUp:
        if (body.size() >= 4) {
            injectKey(type == protocol::kKeyDown, readU16(body, 0), readU16(body, 2) != 0);
        }
        break;
    default:
        break;
    }
}

void InputInjector::injectMouseMove(uint16_t nx, uint16_t ny)
{
#ifdef _WIN32
    const int sw = qMax(1, m_streamSize.width());
    const int sh = qMax(1, m_streamSize.height());
    const int px = int(double(nx) / 65535.0 * qMax(1, sw - 1));
    const int py = int(double(ny) / 65535.0 * qMax(1, sh - 1));

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int absX = sw > 1 ? int(double(px) / double(sw - 1) * qMax(1, screenW - 1)) : 0;
    const int absY = sh > 1 ? int(double(py) / double(sh - 1) * qMax(1, screenH - 1)) : 0;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.dx = LONG(double(absX) / qMax(1, screenW - 1) * 65535);
    input.mi.dy = LONG(double(absY) / qMax(1, screenH - 1) * 65535);
    SendInput(1, &input, sizeof(INPUT));
#else
    Q_UNUSED(nx);
    Q_UNUSED(ny);
#endif
}

void InputInjector::injectMouseButton(uint8_t button, bool down, uint16_t nx, uint16_t ny)
{
#ifdef _WIN32
    injectMouseMove(nx, ny);
    INPUT input{};
    input.type = INPUT_MOUSE;
    DWORD flag = 0;
    if (button == 0) {
        flag = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (button == 1) {
        flag = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else {
        flag = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    }
    input.mi.dwFlags = flag;
    SendInput(1, &input, sizeof(INPUT));
#else
    Q_UNUSED(button);
    Q_UNUSED(down);
    Q_UNUSED(nx);
    Q_UNUSED(ny);
#endif
}

void InputInjector::injectWheel(int16_t delta, uint16_t nx, uint16_t ny)
{
#ifdef _WIN32
    injectMouseMove(nx, ny);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = DWORD(delta);
    SendInput(1, &input, sizeof(INPUT));
#else
    Q_UNUSED(delta);
    Q_UNUSED(nx);
    Q_UNUSED(ny);
#endif
}

void InputInjector::injectKey(bool down, uint16_t vk, bool extended)
{
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    if (!down) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    SendInput(1, &input, sizeof(INPUT));
#else
    Q_UNUSED(down);
    Q_UNUSED(vk);
    Q_UNUSED(extended);
#endif
}

} // namespace darpan::platform
