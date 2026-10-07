#include "ControlProtocol.h"

namespace darpan::protocol {

namespace {

void appendU16(QByteArray &out, uint16_t v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void appendI16(QByteArray &out, int16_t v)
{
    appendU16(out, uint16_t(v));
}

QByteArray header(uint8_t type)
{
    QByteArray out;
    out.append(char(kVersion));
    out.append(char(type));
    return out;
}

} // namespace

QByteArray packMouseMove(uint16_t nx, uint16_t ny)
{
    QByteArray out = header(kMouseMove);
    appendU16(out, nx);
    appendU16(out, ny);
    return out;
}

QByteArray packMouseButton(uint8_t button, bool down, uint16_t nx, uint16_t ny)
{
    QByteArray out = header(kMouseButton);
    out.append(char(button));
    out.append(char(down ? 1 : 0));
    appendU16(out, nx);
    appendU16(out, ny);
    return out;
}

QByteArray packMouseWheel(int16_t delta, uint16_t nx, uint16_t ny)
{
    QByteArray out = header(kMouseWheel);
    appendI16(out, delta);
    appendU16(out, nx);
    appendU16(out, ny);
    return out;
}

QByteArray packKey(bool down, uint16_t vk, bool extended)
{
    QByteArray out = header(down ? kKeyDown : kKeyUp);
    appendU16(out, vk);
    appendU16(out, extended ? 1 : 0);
    return out;
}

bool parseControlMessage(const QByteArray &payload, uint8_t *typeOut, QByteArray *bodyOut)
{
    if (payload.size() < 2 || uint8_t(payload[0]) != kVersion) {
        return false;
    }
    *typeOut = uint8_t(payload[1]);
    *bodyOut = payload.mid(2);
    return true;
}

} // namespace darpan::protocol
