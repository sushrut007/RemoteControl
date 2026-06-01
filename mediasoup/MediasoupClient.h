#pragma once

#include <QObject>
#include <QMutex>
#include <QString>

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

// Forward declarations
namespace mediasoup {
    class IMediasoup;
    class IWorker;
}

class MediasoupClient : public QObject
{
    Q_OBJECT

public:
    // ---------------------------------------------------------------------------
    // Callback types
    // ---------------------------------------------------------------------------

    /// Called when a send transport needs to connect (provide DTLS parameters
    /// back to the server). Receives the transport id and the dtlsParameters JSON.
    using TransportConnectCallback = std::function<void(const std::string& transportId,
        const nlohmann::json& dtlsParameters)>;

    /// Called when a producer has been created. Receives the producer id assigned
    /// by the server and the kind ("audio"/"video").
    using ProduceCallback = std::function<void(const std::string& producerId,
        const std::string& kind)>;

    /// Called when a consumer has been created. Receives the consumer id.
    using ConsumeCallback = std::function<void(const std::string& consumerId,
        const nlohmann::json& consumerInfo)>;

    /// Called when a data producer has been created. Receives the data producer id.
    using ProduceDataCallback = std::function<void(const std::string& dataProducerId,
        const std::string& label)>;

    /// Called when a data consumer has been created. Receives the data consumer id.
    using ConsumeDataCallback = std::function<void(const std::string& dataConsumerId,
        const nlohmann::json& consumerInfo)>;

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    explicit MediasoupClient(QObject* parent = nullptr);
    ~MediasoupClient() override;

    // Non-copyable, non-movable (owns Qt resources)
    MediasoupClient(const MediasoupClient&) = delete;
    MediasoupClient& operator=(const MediasoupClient&) = delete;
    MediasoupClient(MediasoupClient&&) = delete;
    MediasoupClient& operator=(MediasoupClient&&) = delete;

    // ---------------------------------------------------------------------------
    // Device
    // ---------------------------------------------------------------------------

    /// Load device RTP capabilities received from the server (routerRtpCapabilities).
    /// Must be called once before creating transports.
    void loadDevice(const nlohmann::json& rtpCapabilities);

    // ---------------------------------------------------------------------------
    // Transports
    // ---------------------------------------------------------------------------

    /// Create a send transport from the server-provided transport parameters.
    /// @param transportParams  JSON object containing id, iceParameters,
    ///                         iceCandidates, dtlsParameters (and optionally
    ///                         sctpParameters).
    void createSendTransport(const nlohmann::json& transportParams);

    /// Create a receive transport from the server-provided transport parameters.
    void createRecvTransport(const nlohmann::json& transportParams);

    // ---------------------------------------------------------------------------
    // Producers / Consumers
    // ---------------------------------------------------------------------------

    /// Begin producing media of the given kind ("audio" or "video").
    /// @param kind  "audio" or "video"
    /// @param cb    Called (on the Qt main thread) once the producer id is known.
    void produce(const std::string& kind, ProduceCallback cb);

    /// Begin consuming media described by consumerParams.
    /// @param consumerParams  JSON with id, producerId, kind, rtpParameters.
    /// @param cb              Called (on the Qt main thread) on success.
    void consume(const nlohmann::json& consumerParams, ConsumeCallback cb);

    // ---------------------------------------------------------------------------
    // Data Producers / Consumers (SCTP DataChannels)
    // ---------------------------------------------------------------------------

    /// Begin producing a DataChannel with the given label.
    void produceData(const std::string& label, ProduceDataCallback cb);

    /// Begin consuming a DataChannel described by params.
    void consumeData(const nlohmann::json& params, ConsumeDataCallback cb);

    // ---------------------------------------------------------------------------
    // Accessors (thread-safe)
    // ---------------------------------------------------------------------------

    bool isDeviceLoaded() const;

    /// Returns a snapshot of current RTP capabilities (empty object if not loaded).
    nlohmann::json rtpCapabilities() const;

signals:
    // ---------------------------------------------------------------------------
    // Qt signals – always emitted on the Qt main thread
    // ---------------------------------------------------------------------------

    /// Emitted when a transport (send or recv) has connected successfully.
    /// @param transportId  The transport id string.
    /// @param direction    "send" or "recv"
    void transportConnected(const QString& transportId, const QString& direction);

    /// Emitted when a producer has been registered with the server.
    /// @param producerId  Server-assigned producer id.
    /// @param kind        "audio" or "video"
    void producerCreated(const QString& producerId, const QString& kind);

    /// Emitted when a consumer has been created locally.
    /// @param consumerId  The consumer id.
    void consumerCreated(const QString& consumerId);

    /// Emitted when any mediasoup or WebRTC error occurs.
    /// @param message  Human-readable error description.
    void mediaError(const QString& message);

private:
    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    /// Validate that the device has been loaded; emits mediaError and returns
    /// false if not.
    bool ensureDeviceLoaded(const char* context);

    /// Validate that a send transport exists.
    bool ensureSendTransport(const char* context);

    /// Validate that a recv transport exists.
    bool ensureRecvTransport(const char* context);

    /// Marshal a callable to the Qt main thread via QMetaObject::invokeMethod.
    template<typename Fn>
    void invokeOnMainThread(Fn&& fn);

    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------

    mutable QMutex m_mutex; ///< Protects all mutable state below

    bool            m_deviceLoaded{ false };
    nlohmann::json  m_rtpCapabilities;

    // Transport state
    struct TransportState {
        std::string id;
        nlohmann::json params;
        bool connected{ false };
    };

    std::unique_ptr<TransportState> m_sendTransport;
    std::unique_ptr<TransportState> m_recvTransport;

    // Producer registry: producerId -> kind
    std::unordered_map<std::string, std::string> m_producers;

    // Consumer registry: consumerId -> consumerInfo JSON
    std::unordered_map<std::string, nlohmann::json> m_consumers;

    // Data producer registry: dataProducerId -> label
    std::unordered_map<std::string, std::string> m_dataProducers;

    // Data consumer registry: dataConsumerId -> params JSON
    std::unordered_map<std::string, nlohmann::json> m_dataConsumers;
};