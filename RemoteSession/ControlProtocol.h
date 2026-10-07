#pragma once

#include <QByteArray>
#include <cstdint>

namespace darpan::protocol {

constexpr uint8_t kVersion = 0x01;

constexpr uint8_t kMouseMove = 0x10;
constexpr uint8_t kMouseButton = 0x11;
constexpr uint8_t kMouseWheel = 0x12;
constexpr uint8_t kKeyDown = 0x20;
constexpr uint8_t kKeyUp = 0x21;

constexpr uint8_t kFileOffer = 0x30;
constexpr uint8_t kFileAccept = 0x31;
constexpr uint8_t kFileReject = 0x32;
constexpr uint8_t kFileChunk = 0x33;
constexpr uint8_t kFileComplete = 0x34;
constexpr uint8_t kFileCancel = 0x35;

QByteArray packMouseMove(uint16_t nx, uint16_t ny);
QByteArray packMouseButton(uint8_t button, bool down, uint16_t nx, uint16_t ny);
QByteArray packMouseWheel(int16_t delta, uint16_t nx, uint16_t ny);
QByteArray packKey(bool down, uint16_t vk, bool extended);

bool parseControlMessage(const QByteArray &payload, uint8_t *typeOut, QByteArray *bodyOut);

} // namespace darpan::protocol
