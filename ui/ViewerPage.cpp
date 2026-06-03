#include "ViewerPage.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QShortcut>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QPainter>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QMutexLocker>
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QKeySequence>
#include <QDebug>
#include <QLabel>

// ---------------------------------------------------------------------------
// GLSL shaders  (GLSL 1.30 – compatible with OpenGL 3.x)
// ---------------------------------------------------------------------------

static const char* k_vertSrc = R"GLSL(
    #version 130
    in  vec2 aPos;
    in  vec2 aUV;
    out vec2 vUV;
    void main() {
      vUV = aUV;
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)GLSL";

static const char* k_fragSrc = R"GLSL(
    #version 130
    in  vec2      vUV;
    out vec4      fragColor;
uniform sampler2D uTex;
    void main() {
     fragColor = texture(uTex, vUV);
    }
)GLSL";

// Full-screen quad: 2× (position xy, uv xy)
static const float k_quadVerts[] = {
    -1.f, -1.f,  0.f, 1.f,   // bottom-left
     1.f, -1.f,  1.f, 1.f,   // bottom-right
    -1.f,  1.f,  0.f, 0.f,   // top-left
     1.f,  1.f,  1.f, 0.f,   // top-right
};

// ===========================================================================
// FrameRenderer
// ===========================================================================

FrameRenderer::FrameRenderer(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 0);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0); // disable vsync – we drive the rate ourselves
    setFormat(fmt);

    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

FrameRenderer::~FrameRenderer()
{
    makeCurrent();
    delete m_program;
    delete m_vbo;
    delete m_vao;
    delete m_texture;
    doneCurrent();
}

void FrameRenderer::uploadFrame(const QImage& frame)
{
    if (frame.isNull()) { return; }
    QMutexLocker lock(&m_frameMutex);
    m_pendingFrame = frame.convertToFormat(QImage::Format_RGBA8888);
    m_frameDirty = true;
    m_frameWidth = frame.width();
    m_frameHeight = frame.height();
    lock.unlock();
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void FrameRenderer::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.05f, 0.05f, 0.12f, 1.f);

    // Shader program
    m_program = new QOpenGLShaderProgram(this);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, k_vertSrc) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, k_fragSrc) ||
        !m_program->link())
    {
        qWarning() << "[FrameRenderer] Shader error:" << m_program->log();
    }
    m_texUniform = m_program->uniformLocation("uTex");

    // VAO + VBO
    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vao->bind();

    m_vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_vbo->create();
    m_vbo->bind();
    m_vbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vbo->allocate(k_quadVerts, sizeof(k_quadVerts));

    const int posLoc = m_program->attributeLocation("aPos");
    const int uvLoc = m_program->attributeLocation("aUV");
    m_program->enableAttributeArray(posLoc);
    m_program->setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->enableAttributeArray(uvLoc);
    m_program->setAttributeBuffer(uvLoc, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    m_vao->release();
    m_vbo->release();

    // Placeholder texture
    m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_texture->setMinificationFilter(QOpenGLTexture::Linear);
    m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
}

void FrameRenderer::resizeGL(int w, int h)
{
    // Full clear area – letterboxed viewport is set per-frame in paintGL.
    glViewport(0, 0, w, h);
}

void FrameRenderer::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    {
        QMutexLocker lock(&m_frameMutex);
        if (m_frameDirty && !m_pendingFrame.isNull()) {
            if (!m_texture->isCreated() ||
                m_texture->width() != m_pendingFrame.width() ||
                m_texture->height() != m_pendingFrame.height())
            {
                m_texture->destroy();
                m_texture->create();
                m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
                m_texture->setSize(m_pendingFrame.width(), m_pendingFrame.height());
                m_texture->allocateStorage();
            }
            m_texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                m_pendingFrame.constBits());
            m_frameDirty = false;
        }
    }

    if (!m_texture->isCreated()) { return; }

    // Letterbox viewport: scale contentRect from logical → device pixels
    // so the viewport is correct on HiDPI / scaled displays.
    const qreal dpr = devicePixelRatio();
    const QRect cr = contentRect();
    const int vpX = static_cast<int>(cr.x() * dpr);
    const int vpY = static_cast<int>(cr.y() * dpr);
    const int vpW = static_cast<int>(cr.width() * dpr);
    const int vpH = static_cast<int>(cr.height() * dpr);
    // OpenGL Y is bottom-up; widget height in device pixels = height()*dpr
    glViewport(vpX,
        static_cast<int>(height() * dpr) - vpY - vpH,
        vpW, vpH);

    m_program->bind();
    m_texture->bind(0);
    m_program->setUniformValue(m_texUniform, 0);

    m_vao->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao->release();

    m_texture->release();
    m_program->release();

    // Restore full device-pixel viewport for the next glClear.
    glViewport(0, 0,
        static_cast<int>(width() * dpr),
        static_cast<int>(height() * dpr));
}

QRect FrameRenderer::contentRect() const
{
    // Access frame dimensions; safe to read on main/GL thread without a lock
    // because they are only written during uploadFrame which runs before the
    // queued "update" call that triggers paintGL.
    const int fw = m_frameWidth;
    const int fh = m_frameHeight;

    if (fw <= 0 || fh <= 0) {
        return rect(); // no frame yet – fill the widget
    }

    const int ww = width();
    const int wh = height();

    // Scale to fit while preserving aspect ratio.
    const float scale = qMin(static_cast<float>(ww) / fw,
        static_cast<float>(wh) / fh);
    const int cw = static_cast<int>(fw * scale);
    const int ch = static_cast<int>(fh * scale);
    const int cx = (ww - cw) / 2;
    const int cy = (wh - ch) / 2;
    return QRect(cx, cy, cw, ch);
}

// ===========================================================================
// HudOverlay
// ===========================================================================

HudOverlay::HudOverlay(QWidget* parent)
    : QWidget(parent)
    , m_hideTimer(new QTimer(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(3000);
    QObject::connect(m_hideTimer, &QTimer::timeout,
        this, &QWidget::hide);
    QWidget::hide();
}

void HudOverlay::updateInfo(const ConnectionInfo& info)
{
    m_info = info;
    update();
}

void HudOverlay::show()
{
    QWidget::show();
    m_hideTimer->start();
}

void HudOverlay::paintEvent(QPaintEvent*)
{
    const QString text =
        QStringLiteral("Latency: %1 ms   FPS: %2   %3×%4   %5 kbps")
        .arg(m_info.latencyMs)
        .arg(m_info.fps, 0, 'f', 1)
        .arg(m_info.width)
        .arg(m_info.height)
        .arg(m_info.bitrateKbps);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont f = p.font();
    f.setPointSize(10);
    p.setFont(f);

    const QFontMetrics fm(f);
    const int padding = 8;
    const QRect textRect = fm.boundingRect(text);
    const QRect bg(0, 0, textRect.width() + padding * 2, textRect.height() + padding * 2);

    p.fillRect(bg, QColor(0, 0, 0, 160));
    p.setPen(QColor(0xe0, 0xe0, 0xff));
    p.drawText(bg.adjusted(padding, padding, -padding, -padding),
        Qt::AlignLeft | Qt::AlignVCenter, text);
}

// ===========================================================================
// ViewerPage
// ===========================================================================

ViewerPage::ViewerPage(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Ensure the widget background is always black so no palette colour
    // bleeds through around the OpenGL renderer or letterbox bars.
    setAutoFillBackground(false);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    // Renderer fills the whole widget
    m_renderer = new FrameRenderer(this);
    m_renderer->setGeometry(rect());

    // Centred waiting overlay – visible until first frame arrives
    m_waitLabel = new QLabel(this);
    m_waitLabel->setAlignment(Qt::AlignCenter);
    m_waitLabel->setWordWrap(true);
    m_waitLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #a0a0c0; font-size: 16px; background: transparent; }"));
    m_waitLabel->setText(QStringLiteral("Waiting for host to start sharing…"));
    m_waitLabel->setGeometry(rect());
    m_waitLabel->raise();
    // renderer hidden until a frame arrives
    m_renderer->hide();

    // HUD overlay (top-right corner, sized in resizeEvent)
    m_hud = new HudOverlay(this);

    // F11 fullscreen toggle
    m_fullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    QObject::connect(m_fullscreenShortcut, &QShortcut::activated,
        this, &ViewerPage::toggleFullscreen);

    m_fpsTimer.start();
}

void ViewerPage::updateFrame(const QImage& frame)
{
    if (!m_renderer->isVisible()) {
        m_waitLabel->hide();
        m_renderer->show();
    }

    m_renderer->uploadFrame(frame);

    // Reposition HUD whenever a new frame arrives (dimensions may have changed).
    repositionOverlays();

    ++m_frameCount;
    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_connInfo.fps = m_frameCount * 1000.0 / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
        m_hud->updateInfo(m_connInfo);
    }
}

void ViewerPage::setConnectionInfo(const ConnectionInfo& info)
{
    m_connInfo = info;
    m_hud->updateInfo(info);
    m_hud->show();
}

void ViewerPage::showWaitingOverlay(const QString& message)
{
    m_renderer->hide();
    m_waitLabel->setText(message);
    m_waitLabel->show();
    m_waitLabel->raise();
}

void ViewerPage::hideWaitingOverlay()
{
    m_waitLabel->hide();
    m_renderer->show();
}

// ---------------------------------------------------------------------------
// Paint – fill background black so no palette colour bleeds around the GL widget
// ---------------------------------------------------------------------------
void ViewerPage::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ViewerPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_renderer->setGeometry(rect()); // renderer always fills full widget
    repositionOverlays();
}

void ViewerPage::repositionOverlays()
{
    // Place overlays relative to the actual rendered content rectangle so they
    // are never offset into the letterbox bars.
    const QRect cr = m_renderer->contentRect();

    // waitLabel covers only the content area (centred text inside it).
    m_waitLabel->setGeometry(cr);

    // HUD: top-right corner of the content area, flush with the top edge.
    const int hudW = 380;
    const int hudH = 30;
    m_hud->setGeometry(cr.right() - hudW - 4, cr.top(), hudW, hudH);
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

MouseData ViewerPage::buildMouseData(QMouseEvent* ev, const QString& type) const
{
    MouseData d;
    d.type = type;

    // Map cursor position into the letterboxed content rect so the normalised
    // [0,1] coordinates correspond exactly to what the host rendered.
    const QRect cr = m_renderer->contentRect();
    const float rx = cr.width() > 0 ? static_cast<float>(ev->pos().x() - cr.x()) / cr.width() : 0.f;
    const float ry = cr.height() > 0 ? static_cast<float>(ev->pos().y() - cr.y()) / cr.height() : 0.f;
    d.x = qBound(0.f, rx, 1.f);
    d.y = qBound(0.f, ry, 1.f);

    const Qt::MouseButton btn = ev->button() == Qt::NoButton
        ? ev->buttons().testFlag(Qt::LeftButton) ? Qt::LeftButton : Qt::NoButton
        : ev->button();
    if (btn == Qt::LeftButton) { d.button = QStringLiteral("left"); }
    else if (btn == Qt::RightButton) { d.button = QStringLiteral("right"); }
    else if (btn == Qt::MiddleButton) { d.button = QStringLiteral("middle"); }
    else { d.button = QStringLiteral("left"); }

    return d;
}

void ViewerPage::mouseMoveEvent(QMouseEvent* ev)
{
    emit mouseEvent(buildMouseData(ev, QStringLiteral("mousemove")));
    m_hud->show();
}

void ViewerPage::mousePressEvent(QMouseEvent* ev)
{
    setFocus();
    auto d = buildMouseData(ev, QStringLiteral("mousedown"));
    emit mouseEvent(d);
}

void ViewerPage::mouseReleaseEvent(QMouseEvent* ev)
{
    emit mouseEvent(buildMouseData(ev, QStringLiteral("mouseup")));
    // Synthesise click
    emit mouseEvent(buildMouseData(ev, QStringLiteral("click")));
}

void ViewerPage::wheelEvent(QWheelEvent* ev)
{
    MouseData d;
    d.type = QStringLiteral("wheel");

    // Same letterbox-aware normalisation as buildMouseData.
    const QRect cr = m_renderer->contentRect();
    const float rx = cr.width() > 0 ? static_cast<float>(ev->position().x() - cr.x()) / cr.width() : 0.f;
    const float ry = cr.height() > 0 ? static_cast<float>(ev->position().y() - cr.y()) / cr.height() : 0.f;
    d.x = qBound(0.f, rx, 1.f);
    d.y = qBound(0.f, ry, 1.f);

    const QPoint delta = ev->angleDelta();
    d.deltaX = static_cast<float>(delta.x()) / 120.f;
    d.deltaY = static_cast<float>(delta.y()) / 120.f;
    d.button = QStringLiteral("none");
    emit mouseEvent(d);
}

// ---------------------------------------------------------------------------
// Keyboard events
// ---------------------------------------------------------------------------

QStringList ViewerPage::activeModifiers(Qt::KeyboardModifiers mods) const
{
    QStringList list;
    if (mods & Qt::ControlModifier) { list << QStringLiteral("ctrl"); }
    if (mods & Qt::AltModifier) { list << QStringLiteral("alt"); }
    if (mods & Qt::ShiftModifier) { list << QStringLiteral("shift"); }
    if (mods & Qt::MetaModifier) { list << QStringLiteral("meta"); }
    return list;
}

QString ViewerPage::qtKeyToName(int key, const QString& text) const
{
    // Use the printable text if it's a single non-control character.
    if (text.length() == 1 && text.at(0).isPrint()) {
        return text;
    }

    // Named keys
    switch (key) {
    case Qt::Key_Return:    return QStringLiteral("Enter");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Delete:    return QStringLiteral("Delete");
    case Qt::Key_Tab:       return QStringLiteral("Tab");
    case Qt::Key_Escape:    return QStringLiteral("Escape");
    case Qt::Key_Space:     return QStringLiteral(" ");
    case Qt::Key_Left:      return QStringLiteral("ArrowLeft");
    case Qt::Key_Right:     return QStringLiteral("ArrowRight");
    case Qt::Key_Up:        return QStringLiteral("ArrowUp");
    case Qt::Key_Down:      return QStringLiteral("ArrowDown");
    case Qt::Key_Home:      return QStringLiteral("Home");
    case Qt::Key_End:       return QStringLiteral("End");
    case Qt::Key_PageUp:    return QStringLiteral("PageUp");
    case Qt::Key_PageDown:  return QStringLiteral("PageDown");
    case Qt::Key_Insert:    return QStringLiteral("Insert");
    case Qt::Key_F1:        return QStringLiteral("F1");
    case Qt::Key_F2:        return QStringLiteral("F2");
    case Qt::Key_F3:        return QStringLiteral("F3");
    case Qt::Key_F4:        return QStringLiteral("F4");
    case Qt::Key_F5:        return QStringLiteral("F5");
    case Qt::Key_F6:        return QStringLiteral("F6");
    case Qt::Key_F7:        return QStringLiteral("F7");
    case Qt::Key_F8:        return QStringLiteral("F8");
    case Qt::Key_F9:        return QStringLiteral("F9");
    case Qt::Key_F10:       return QStringLiteral("F10");
    case Qt::Key_F11:       return QStringLiteral("F11");
    case Qt::Key_F12:       return QStringLiteral("F12");
    case Qt::Key_Control:   return QStringLiteral("Control");
    case Qt::Key_Shift:     return QStringLiteral("Shift");
    case Qt::Key_Alt:       return QStringLiteral("Alt");
    case Qt::Key_Meta:      return QStringLiteral("Meta");
    default:                return QString();
    }
}

void ViewerPage::keyPressEvent(QKeyEvent* ev)
{
    const QString name = qtKeyToName(ev->key(), ev->text());
    if (name.isEmpty()) { return; }

    KeyboardData d;
    d.key = name;
    d.type = QStringLiteral("keydown");
    d.modifiers = activeModifiers(ev->modifiers());
    emit keyboardEvent(d);
}

void ViewerPage::keyReleaseEvent(QKeyEvent* ev)
{
    const QString name = qtKeyToName(ev->key(), ev->text());
    if (name.isEmpty()) { return; }

    KeyboardData d;
    d.key = name;
    d.type = QStringLiteral("keyup");
    d.modifiers = activeModifiers(ev->modifiers());
    emit keyboardEvent(d);
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void ViewerPage::contextMenuEvent(QContextMenuEvent* ev)
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background:#16213e; color:#dcdcf0; border:1px solid #2a2a5e; border-radius:6px; padding:4px 0; }"
        "QMenu::item { padding:7px 24px; font-size:13px; }"
        "QMenu::item:selected { background:#2a2a6e; }"
        "QMenu::separator { height:1px; background:#2a2a5e; margin:3px 10px; }"));

    QAction* screenshotAct = menu.addAction(QStringLiteral("Take Screenshot"));
    QAction* fullscreenAct = menu.addAction(QStringLiteral("Toggle Fullscreen\tF11"));
    menu.addSeparator();
    QAction* disconnectAct = menu.addAction(QStringLiteral("Disconnect"));

    QAction* chosen = menu.exec(ev->globalPos());
    if (chosen == screenshotAct) { takeScreenshot(); }
    else if (chosen == fullscreenAct) { toggleFullscreen(); }
    else if (chosen == disconnectAct) { emit disconnectRequested(); }
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

void ViewerPage::takeScreenshot()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation);
    QDir().mkpath(dir);

    const QString path = dir + QDir::separator() +
        QStringLiteral("screenshot_%1.png")
        .arg(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss")));

    const QPixmap px = m_renderer->grab();
    if (px.save(path)) {
        qDebug() << "[ViewerPage] screenshot saved:" << path;
    }
    else {
        qWarning() << "[ViewerPage] failed to save screenshot:" << path;
    }
}

// ---------------------------------------------------------------------------
// Fullscreen
// ---------------------------------------------------------------------------

void ViewerPage::toggleFullscreen()
{
    QWidget* top = window();
    if (top->isFullScreen()) {
        top->showNormal();
    }
    else {
        top->showFullScreen();
    }
}