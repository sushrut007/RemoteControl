#pragma once

#include <QWidget>
#include <QImage>
#include <QString>
#include <QDateTime>
#include "../core/AppState.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QListWidgetItem)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTimer)
QT_FORWARD_DECLARE_CLASS(QElapsedTimer)


// ---------------------------------------------------------------------------
// QrCodeWidget – renders a QR code from a URL without external lib dependency
// Implements a minimal QR matrix via a bundled data blob; falls back to URL
// text if encoding is unavailable.  If libqrencode is linked it is used.
// ---------------------------------------------------------------------------
class QrCodeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QrCodeWidget(QWidget* parent = nullptr);
    ~QrCodeWidget() override;

    void setUrl(const QString& url);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void        regenerate();

    QString     m_url;
    QImage      m_qrImage; ///< 1-bit-per-module image scaled in paintEvent
    bool        m_valid{ false };
};

// ---------------------------------------------------------------------------
// PreviewWidget – shows scaled-down local screen thumbnail
// ---------------------------------------------------------------------------
class PreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void updateFrame(const QImage& frame);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_frame;
};

// ---------------------------------------------------------------------------
// HostPage
// ---------------------------------------------------------------------------
class HostPage : public QWidget
{
    Q_OBJECT
public:
    explicit HostPage(QWidget* parent = nullptr);
    ~HostPage() override = default;

    // Feed the current local capture frame for the preview thumbnail.
    void updatePreviewFrame(const QImage& frame);

    // Call when the room is established; populates info panel.
    void setRoomInfo(const QString& roomId,
        const QString& password,
        const QString& serverUrl);

    // Peer list management
    void addPeer(const PeerInfo& peer);
    void removePeer(const QString& peerId);

    // Stream state feedback
    void setSharing(bool active);
    void setControlAllowed(bool allowed);

    // Status bar updates (called periodically by the host controller)
    void setStreamStatus(bool live, int viewerCount);

signals:
    void shareToggled(bool active);
    void controlToggled(bool allowed);
    void kickPeer(const QString& peerId);
    void sessionEnded();

private slots:
    void onShareClicked();
    void onControlClicked();
    void onKickClicked();
    void onEndSessionClicked();
    void onPeerSelectionChanged();
    void onCopyRoomId();
    void onTogglePasswordVisible();
    void onUptimeTick();

private:
    void buildUi();
    void updateStatusBar();
    QString selectedPeerId() const;

    // ── Info panel (left column) ──────────────────────────────────────────
    QLabel* m_roomIdLabel{ nullptr };
    QPushButton* m_copyRoomIdBtn{ nullptr };
    QLabel* m_passwordLabel{ nullptr };
    QPushButton* m_togglePassBtn{ nullptr };
    QrCodeWidget* m_qrWidget{ nullptr };
    QListWidget* m_peerList{ nullptr };

    // ── Preview (bottom-right overlay) ───────────────────────────────────
    PreviewWidget* m_preview{ nullptr };

    // ── Control panel (bottom) ────────────────────────────────────────────
    QPushButton* m_shareBtn{ nullptr };
    QPushButton* m_controlBtn{ nullptr };
    QPushButton* m_kickBtn{ nullptr };
    QPushButton* m_endBtn{ nullptr };

    // ── Status bar ───────────────────────────────────────────────────────
    QLabel* m_statusDot{ nullptr };
    QLabel* m_statusText{ nullptr };
    QLabel* m_viewerCountLabel{ nullptr };
    QLabel* m_uptimeLabel{ nullptr };

    // ── State ────────────────────────────────────────────────────────────
    QString       m_roomId;
    QString       m_password;
    bool          m_sharing{ false };
    bool          m_controlAllowed{ false };
    bool          m_passwordVisible{ false };
    bool          m_streamLive{ false };
    int           m_viewerCount{ 0 };

    QTimer* m_uptimeTimer{ nullptr };
    QElapsedTimer* m_uptimeClock{ nullptr };
};