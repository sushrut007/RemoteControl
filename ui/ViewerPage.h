#pragma once

#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QOpenGLTexture)
QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)
QT_FORWARD_DECLARE_CLASS(QOpenGLBuffer)
QT_FORWARD_DECLARE_CLASS(QOpenGLVertexArrayObject)
QT_FORWARD_DECLARE_CLASS(QShortcut)
QT_FORWARD_DECLARE_CLASS(QLabel)

// ---------------------------------------------------------------------------
// Data structures for outgoing events
// ---------------------------------------------------------------------------

struct MouseData {
    float   x{ 0.f };      ///< Normalised [0,1]
    float   y{ 0.f };
    float   deltaX{ 0.f }; ///< Wheel
    float   deltaY{ 0.f };
    QString type;           ///< "mousemove" | "mousedown" | "mouseup" | "click" | "wheel"
    QString button;         ///< "left" | "right" | "middle"
};

struct KeyboardData {
    QString key;
    QString type;           ///< "keydown" | "keyup"
    QStringList modifiers;
};

struct ConnectionInfo {
    int     latencyMs{ 0 };
    double  fps{ 0.0 };
    int     width{ 0 };
    int     height{ 0 };
    int     bitrateKbps{ 0 };
};

// ---------------------------------------------------------------------------
// FrameRenderer – QOpenGLWidget that uploads and draws one RGBA texture
// ---------------------------------------------------------------------------
class FrameRenderer : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit FrameRenderer(QWidget* parent = nullptr);
    ~FrameRenderer() override;

    /// Thread-safe: may be called from any thread.
    void uploadFrame(const QImage& frame);

    /// Returns the letterboxed rectangle (in widget pixels) where the video
    /// is actually drawn.  (0,0,w,h) until the first frame arrives.
    QRect contentRect() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QMutex  m_frameMutex;
    QImage   m_pendingFrame;
    bool               m_frameDirty{ false };

    // Frame dimensions – written under m_frameMutex, read on main thread only
    int          m_frameWidth{ 0 };
    int       m_frameHeight{ 0 };

    QOpenGLShaderProgram* m_program{ nullptr };
    QOpenGLBuffer* m_vbo{ nullptr };
    QOpenGLVertexArrayObject* m_vao{ nullptr };
    QOpenGLTexture* m_texture{ nullptr };
    int    m_texUniform{ -1 };
};

// ---------------------------------------------------------------------------
// HudOverlay – transparent overlay widget with auto-hide
// ---------------------------------------------------------------------------
class HudOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit HudOverlay(QWidget* parent = nullptr);

    void updateInfo(const ConnectionInfo& info);
    void show();   ///< Shows and restarts the 3 s hide timer
    using QWidget::hide;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ConnectionInfo m_info;
    QTimer* m_hideTimer{ nullptr };
};

// ---------------------------------------------------------------------------
// ViewerPage
// ---------------------------------------------------------------------------
class ViewerPage : public QWidget
{
    Q_OBJECT

public:
    explicit ViewerPage(QWidget* parent = nullptr);
    ~ViewerPage() override = default;

    /// Push a new decoded frame for display (may be called from any thread).
    void updateFrame(const QImage& frame);

    /// Thread-safe accessor for the GL renderer (used for direct frame upload
    /// from the decode thread).
    FrameRenderer* renderer() const { return m_renderer; }

    /// Called on the main thread after a frame has been uploaded to handle
    /// UI visibility changes and FPS stats (must run on main thread).
    void onFrameDelivered();

    /// Update HUD statistics.
    void setConnectionInfo(const ConnectionInfo& info);

    /// Show a centred status message (e.g. "Waiting for host…").
    /// Hides the renderer so the dark background + text is visible.
    void showWaitingOverlay(const QString& message);

    /// Hide the status overlay and show the renderer again.
    void hideWaitingOverlay();

signals:
    void mouseEvent(const MouseData& data);
    void keyboardEvent(const KeyboardData& data);
    void disconnectRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Helpers
    MouseData    buildMouseData(QMouseEvent* ev, const QString& type) const;
    QStringList  activeModifiers(Qt::KeyboardModifiers mods) const;
    QString      qtKeyToName(int key, const QString& text) const;
    void         takeScreenshot();
    void         toggleFullscreen();
    void    repositionOverlays();   ///< recompute HUD + waitLabel geometry

    FrameRenderer* m_renderer{ nullptr };
    HudOverlay* m_hud{ nullptr };
    QShortcut* m_fullscreenShortcut{ nullptr };
    QLabel* m_waitLabel{ nullptr };

    // FPS tracking
    QElapsedTimer  m_fpsTimer;
    int            m_frameCount{ 0 };
    ConnectionInfo m_connInfo;
};