#pragma once

#include <functional>
#include <QString>

namespace darpan::signaling {

/// Client-side signaling API (Phase 3 implementation).
/// Transport: JSON messages over WebSocket — see docs/ARCHITECTURE.md.
class SignalingClient {
public:
    using MessageHandler = std::function<void(const QString &json)>;

    virtual ~SignalingClient() = default;

    virtual void setMessageHandler(MessageHandler handler) = 0;
    virtual void connectToServer(const QString &webSocketUrl) = 0;
    virtual void disconnect() = 0;

    virtual void sendHello(const QString &deviceName) = 0;
    virtual void sendCreateRoom(const QString &pin = {}) = 0;
    virtual void sendJoinRoom(const QString &roomId,
                              const QString &pin = {},
                              const QString &roleHint = {}) = 0;
    virtual void sendLeaveRoom() = 0;
    virtual void sendSignalOffer(const QString &targetDeviceId, const QString &sdp) = 0;
    virtual void sendSignalAnswer(const QString &targetDeviceId, const QString &sdp) = 0;
    virtual void sendSignalIce(const QString &targetDeviceId, const QString &candidateJson) = 0;
    virtual void sendGrantControl(const QString &targetDeviceId) = 0;
    virtual void sendRevokeControl(const QString &targetDeviceId) = 0;
    virtual void sendRequestControl() = 0;
    virtual void sendSetScreenShare(bool active) = 0;
};

} // namespace darpan::signaling
