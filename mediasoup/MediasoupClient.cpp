#include "MediasoupClient.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper – generate a simple sequential id string for local objects.
// In production this would be replaced by server-assigned ids.
// ---------------------------------------------------------------------------
static std::string generateLocalId(const char* prefix)
{
    static std::atomic<uint64_t> counter{ 0 };
    return std::string(prefix) + "_" + std::to_string(++counter);
}

// ---------------------------------------------------------------------------
// MediasoupClient – construction / destruction
// ---------------------------------------------------------------------------

MediasoupClient::MediasoupClient(QObject* parent)
    : QObject(parent)
{
}

MediasoupClient::~MediasoupClient() = default;

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

template<typename Fn>
void MediasoupClient::invokeOnMainThread(Fn&& fn)
{
    // If already on the main thread, invoke directly to avoid an unnecessary
    // event-loop round-trip.
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        fn();
    }
    else {
        // Capture fn by value so the lambda is safe across thread boundaries.
        auto captured = std::forward<Fn>(fn);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [captured]() mutable { captured(); },
            Qt::QueuedConnection);
    }
}

bool MediasoupClient::ensureDeviceLoaded(const char* context)
{
    QMutexLocker lock(&m_mutex);
    if (!m_deviceLoaded) {
        const QString msg = QString("MediasoupClient::%1 – device not loaded. "
            "Call loadDevice() first.").arg(context);
        // Release lock before emitting so slots can call back into us.
        lock.unlock();
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
        return false;
    }
    return true;
}

bool MediasoupClient::ensureSendTransport(const char* context)
{
    QMutexLocker lock(&m_mutex);
    if (!m_sendTransport) {
        const QString msg = QString("MediasoupClient::%1 – send transport not created. "
            "Call createSendTransport() first.").arg(context);
        lock.unlock();
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
        return false;
    }
    return true;
}

bool MediasoupClient::ensureRecvTransport(const char* context)
{
    QMutexLocker lock(&m_mutex);
    if (!m_recvTransport) {
        const QString msg = QString("MediasoupClient::%1 – recv transport not created. "
            "Call createRecvTransport() first.").arg(context);
        lock.unlock();
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

void MediasoupClient::loadDevice(const nlohmann::json& rtpCapabilities)
{
    try {
        // Basic validation – the server JSON must contain a "codecs" array.
        if (!rtpCapabilities.contains("codecs") || !rtpCapabilities["codecs"].is_array()) {
            throw std::invalid_argument("rtpCapabilities must contain a 'codecs' array");
        }

        {
            QMutexLocker lock(&m_mutex);
            m_rtpCapabilities = rtpCapabilities;
            m_deviceLoaded = true;
        }

        // No signal defined for device load in the spec, but we inform callers
        // that operations are now possible by simply not emitting an error.
    }
    catch (const std::exception& ex) {
        const QString msg = QString("loadDevice failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

// ---------------------------------------------------------------------------
// Transports
// ---------------------------------------------------------------------------

void MediasoupClient::createSendTransport(const nlohmann::json& transportParams)
{
    if (!ensureDeviceLoaded("createSendTransport")) {
        return;
    }

    try {
        if (!transportParams.contains("id") || !transportParams["id"].is_string()) {
            throw std::invalid_argument("transportParams must contain a string 'id'");
        }

        const std::string transportId = transportParams["id"].get<std::string>();

        {
            QMutexLocker lock(&m_mutex);
            m_sendTransport = std::make_unique<TransportState>();
            m_sendTransport->id = transportId;
            m_sendTransport->params = transportParams;
            m_sendTransport->connected = false;
        }

        // Simulate the "connect" handshake: in a real integration the
        // dtlsParameters extracted here would be sent to the server, and this
        // method would be called again once the server confirms.
        const nlohmann::json dtlsParams = transportParams.value("dtlsParameters",
            nlohmann::json::object());

        {
            QMutexLocker lock(&m_mutex);
            m_sendTransport->connected = true;
        }

        const QString qId = QString::fromStdString(transportId);
        const QString qDirection = QStringLiteral("send");

        invokeOnMainThread([this, qId, qDirection]() {
            emit transportConnected(qId, qDirection);
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("createSendTransport failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

void MediasoupClient::createRecvTransport(const nlohmann::json& transportParams)
{
    if (!ensureDeviceLoaded("createRecvTransport")) {
        return;
    }

    try {
        if (!transportParams.contains("id") || !transportParams["id"].is_string()) {
            throw std::invalid_argument("transportParams must contain a string 'id'");
        }

        const std::string transportId = transportParams["id"].get<std::string>();

        {
            QMutexLocker lock(&m_mutex);
            m_recvTransport = std::make_unique<TransportState>();
            m_recvTransport->id = transportId;
            m_recvTransport->params = transportParams;
            m_recvTransport->connected = false;
        }

        const nlohmann::json dtlsParams = transportParams.value("dtlsParameters",
            nlohmann::json::object());

        {
            QMutexLocker lock(&m_mutex);
            m_recvTransport->connected = true;
        }

        const QString qId = QString::fromStdString(transportId);
        const QString qDirection = QStringLiteral("recv");

        invokeOnMainThread([this, qId, qDirection]() {
            emit transportConnected(qId, qDirection);
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("createRecvTransport failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

// ---------------------------------------------------------------------------
// Producers
// ---------------------------------------------------------------------------

void MediasoupClient::produce(const std::string& kind, ProduceCallback cb)
{
    if (!ensureDeviceLoaded("produce")) { return; }
    if (!ensureSendTransport("produce")) { return; }

    if (kind != "audio" && kind != "video") {
        const QString msg = QString("produce: unsupported kind '%1'").arg(
            QString::fromStdString(kind));
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
        return;
    }

    try {
        // In a real integration, this is where the native WebRTC PeerConnection
        // AddTrack / CreateOffer flow would be triggered, the resulting RTP
        // parameters sent to the server, and the server-assigned producer id
        // received in the response.  Here we register a local producer and
        // surface the id via the callback and signal.
        const std::string producerId = generateLocalId("producer");

        {
            QMutexLocker lock(&m_mutex);
            m_producers[producerId] = kind;
        }

        const QString qId = QString::fromStdString(producerId);
        const QString qKind = QString::fromStdString(kind);
        ProduceCallback capturedCb = std::move(cb);

        invokeOnMainThread([this, producerId, kind, qId, qKind,
            capturedCb = std::move(capturedCb)]() mutable {
                if (capturedCb) {
                    capturedCb(producerId, kind);
                }
                emit producerCreated(qId, qKind);
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("produce failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

void MediasoupClient::consume(const nlohmann::json& consumerParams, ConsumeCallback cb)
{
    if (!ensureDeviceLoaded("consume")) { return; }
    if (!ensureRecvTransport("consume")) { return; }

    try {
        if (!consumerParams.contains("id") || !consumerParams["id"].is_string()) {
            throw std::invalid_argument("consumerParams must contain a string 'id'");
        }
        if (!consumerParams.contains("kind") || !consumerParams["kind"].is_string()) {
            throw std::invalid_argument("consumerParams must contain a string 'kind'");
        }
        if (!consumerParams.contains("rtpParameters")) {
            throw std::invalid_argument("consumerParams must contain 'rtpParameters'");
        }

        const std::string consumerId = consumerParams["id"].get<std::string>();

        {
            QMutexLocker lock(&m_mutex);
            m_consumers[consumerId] = consumerParams;
        }

        const QString qId = QString::fromStdString(consumerId);
        ConsumeCallback capturedCb = std::move(cb);

        invokeOnMainThread([this, consumerId, consumerParams, qId,
            capturedCb = std::move(capturedCb)]() mutable {
                if (capturedCb) {
                    capturedCb(consumerId, consumerParams);
                }
                emit consumerCreated(qId);
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("consume failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

// ---------------------------------------------------------------------------
// Data Producers / Consumers (SCTP DataChannels)
// ---------------------------------------------------------------------------

void MediasoupClient::produceData(const std::string& label, ProduceDataCallback cb)
{
    if (!ensureDeviceLoaded("produceData")) { return; }
    if (!ensureSendTransport("produceData")) { return; }

    try {
        if (label.empty()) {
            throw std::invalid_argument("DataChannel label must not be empty");
        }

        const std::string dataProducerId = generateLocalId("dataProducer");

        {
            QMutexLocker lock(&m_mutex);
            m_dataProducers[dataProducerId] = label;
        }

        ProduceDataCallback capturedCb = std::move(cb);

        invokeOnMainThread([dataProducerId, label,
            capturedCb = std::move(capturedCb)]() mutable {
                if (capturedCb) {
                    capturedCb(dataProducerId, label);
                }
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("produceData failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

void MediasoupClient::consumeData(const nlohmann::json& params, ConsumeDataCallback cb)
{
    if (!ensureDeviceLoaded("consumeData")) { return; }
    if (!ensureRecvTransport("consumeData")) { return; }

    try {
        if (!params.contains("id") || !params["id"].is_string()) {
            throw std::invalid_argument("params must contain a string 'id'");
        }

        const std::string dataConsumerId = params["id"].get<std::string>();

        {
            QMutexLocker lock(&m_mutex);
            m_dataConsumers[dataConsumerId] = params;
        }

        ConsumeDataCallback capturedCb = std::move(cb);

        invokeOnMainThread([dataConsumerId, params,
            capturedCb = std::move(capturedCb)]() mutable {
                if (capturedCb) {
                    capturedCb(dataConsumerId, params);
                }
            });

    }
    catch (const std::exception& ex) {
        const QString msg = QString("consumeData failed: %1").arg(ex.what());
        invokeOnMainThread([this, msg]() { emit mediaError(msg); });
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool MediasoupClient::isDeviceLoaded() const
{
    QMutexLocker lock(&m_mutex);
    return m_deviceLoaded;
}

nlohmann::json MediasoupClient::rtpCapabilities() const
{
    QMutexLocker lock(&m_mutex);
    return m_deviceLoaded ? m_rtpCapabilities : nlohmann::json::object();
}