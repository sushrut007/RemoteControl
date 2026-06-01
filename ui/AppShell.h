#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QSystemTrayIcon>
#include <QMenu>

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSizeGrip)
QT_FORWARD_DECLARE_CLASS(QHBoxLayout)
QT_FORWARD_DECLARE_CLASS(QVBoxLayout)
QT_FORWARD_DECLARE_CLASS(QGraphicsOpacityEffect)

// ---------------------------------------------------------------------------
// Page identifiers
// ---------------------------------------------------------------------------
enum class PageType {
    Connect,
    Viewer,
    Host
};

// ---------------------------------------------------------------------------
// ModalOverlay – semi-transparent full-window overlay that centers one child.
// ---------------------------------------------------------------------------
class ModalOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit ModalOverlay(QWidget* parent = nullptr);

    void setContent(QWidget* content);
    void clearContent();
    void recenter(); // re-center after content changes size

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override; // absorb clicks
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* m_content{ nullptr };
};

// ---------------------------------------------------------------------------
// AppShell
// ---------------------------------------------------------------------------
class AppShell : public QMainWindow
{
    Q_OBJECT

public:
    explicit AppShell(QWidget* parent = nullptr);
    ~AppShell() override;

    // -----------------------------------------------------------------------
    // Pages
    // -----------------------------------------------------------------------

    /// Switch to the named page with a fade transition.
    void showPage(PageType page);

    /// Replace the placeholder page widget with a real page.
    /// The old placeholder is deleted. The new widget is re-parented.
    void setPageWidget(PageType page, QWidget* widget);

    // -----------------------------------------------------------------------
    // Modal
    // -----------------------------------------------------------------------

    /// Display @p modal centered over a semi-transparent overlay.
    void showModal(QWidget* modal);

    /// Hide and destroy the modal overlay content.
    void hideModal();

    /// Re-center the active modal (call when modal content changes size).
    void recenterModal();

    // -----------------------------------------------------------------------
    // Connection status
    // -----------------------------------------------------------------------

    enum class ConnectionStatus { Disconnected, Connecting, Connected };
    void setConnectionStatus(ConnectionStatus status);

signals:
    void settingsRequested();
    void disconnectRequested();
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void buildUi();
    void buildTray();
    void loadStyleSheet();
    void applyFadeTransition(QWidget* outgoing, QWidget* incoming);

    // -----------------------------------------------------------------------
    // Widgets
    // -----------------------------------------------------------------------

    // Top bar
    QWidget* m_topBar{ nullptr };
    QLabel* m_logoLabel{ nullptr };
    QLabel* m_statusDot{ nullptr };
    QPushButton* m_settingsBtn{ nullptr };
    QPushButton* m_minimizeBtn{ nullptr };
    QPushButton* m_maximizeBtn{ nullptr };
    QPushButton* m_closeBtn{ nullptr };

    // Page stack
    QStackedWidget* m_stack{ nullptr };
    QWidget* m_connectPage{ nullptr };
    QWidget* m_viewerPage{ nullptr };
    QWidget* m_hostPage{ nullptr };

    // Fade animation
    QGraphicsOpacityEffect* m_fadeEffect{ nullptr };
    QPropertyAnimation* m_fadeAnim{ nullptr };

    // Modal overlay
    ModalOverlay* m_overlay{ nullptr };

    // Frameless-window resize grip
    QSizeGrip* m_sizeGrip{ nullptr };

    // System tray
    QSystemTrayIcon* m_trayIcon{ nullptr };
    QMenu* m_trayMenu{ nullptr };

    // Window dragging (frameless)
    bool   m_dragging{ false };
    QPoint m_dragOffset;
};