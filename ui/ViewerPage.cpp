#include "ViewerPage.h"
#include "DarpanIcons.h"
#include "DarpanTheme.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QPushButton>
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
#include <QThread>

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

    // Accept Format_RGB32/ARGB32 directly (BGRA in memory on LE Windows).
    QImage toUpload;
    if (frame.format() == QImage::Format_RGB32 ||
        frame.format() == QImage::Format_ARGB32)
    {
        toUpload = frame; // no copy – implicit sharing
    }
    else {
        toUpload = frame.convertToFormat(QImage::Format_ARGB32);
    }

    {
        QMutexLocker lock(&m_frameMutex);
        m_pendingFrame = toUpload;
        m_frameDirty = true;
        m_frameWidth = toUpload.width();
        m_frameHeight = toUpload.height();
    }

    // Schedule a repaint. QWidget::update() is NOT thread-safe, so if called
    // from a non-GUI thread we must post via invokeMethod.
    if (QThread::currentThread() == this->thread()) {
        update();
    }
    else {
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    }
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
    m_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
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
            // Upload BGRA data directly – the GPU swizzles B↔R in hardware,
                  // which is essentially free vs the CPU-side per-pixel conversion.
            m_texture->setData(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8,
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
    const bool controlling = m_info.role == QLatin1String("controller");
    const QColor accent = controlling
        ? QColor(DarpanTheme::kRoleController)
        : QColor(DarpanTheme::kRoleViewer);

    const QString text =
        QStringLiteral("Latency %1 ms  •  %2 FPS  •  %3×%4  •  %5 kbps")
        .arg(m_info.latencyMs)
        .arg(m_info.fps, 0, 'f', 1)
        .arg(m_info.width)
        .arg(m_info.height)
        .arg(m_info.bitrateKbps);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont f = p.font();
    f.setPointSize(9);
    f.setFamily(QStringLiteral("Consolas"));
    p.setFont(f);

    const QFontMetrics fm(f);
    const int padding = 10;
    const QRect textRect = fm.boundingRect(text);
    const QRect bg(0, 0, textRect.width() + padding * 2, textRect.height() + padding);

    p.setPen(QColor(DarpanTheme::kBorderSubtle));
    p.setBrush(QColor(33, 38, 45, 230));
    p.drawRoundedRect(bg, 14, 14);

    p.setPen(accent);
    p.drawText(bg.adjusted(padding, padding / 2, -padding, -padding / 2),
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

    m_renderer = new FrameRenderer(this);
    m_renderer->setGeometry(rect());
    m_renderer->hide();

    m_waitOverlay = new QWidget(this);
    m_waitOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* waitLayout = new QVBoxLayout(m_waitOverlay);
    waitLayout->setAlignment(Qt::AlignCenter);

    auto* iconCircle = new QLabel(m_waitOverlay);
    iconCircle->setObjectName(QStringLiteral("WaitIconCircle"));
    iconCircle->setFixedSize(64, 64);
    iconCircle->setAlignment(Qt::AlignCenter);
    iconCircle->setPixmap(DarpanIcons::tintedPixmap(
        QStringLiteral("wifi_tethering"), QColor(DarpanTheme::kRoleViewer), QSize(32, 32)));

    m_waitTitle = new QLabel(tr("Waiting for host to start sharing…"), m_waitOverlay);
    m_waitTitle->setObjectName(QStringLiteral("WaitTitle"));
    m_waitTitle->setAlignment(Qt::AlignCenter);

    m_waitSubtitle = new QLabel(
        tr("You'll see the remote screen here once sharing begins."),
        m_waitOverlay);
    m_waitSubtitle->setObjectName(QStringLiteral("WaitSubtitle"));
    m_waitSubtitle->setAlignment(Qt::AlignCenter);
    m_waitSubtitle->setWordWrap(true);
    m_waitSubtitle->setMaximumWidth(360);

    m_roomIdFooter = new QLabel(m_waitOverlay);
    m_roomIdFooter->setObjectName(QStringLiteral("RoomIdFooter"));
    m_roomIdFooter->setAlignment(Qt::AlignCenter);

    waitLayout->addWidget(iconCircle, 0, Qt::AlignHCenter);
    waitLayout->addSpacing(12);
    waitLayout->addWidget(m_waitTitle);
    waitLayout->addWidget(m_waitSubtitle);
    waitLayout->addStretch();
    waitLayout->addWidget(m_roomIdFooter, 0, Qt::AlignHCenter);
    m_roomIdFooter->hide();

    m_roleBadge = new QLabel(this);
    m_roleBadge->setObjectName(QStringLiteral("RoleBadge"));
    m_roleBadge->hide();

    m_hud = new HudOverlay(this);

    m_sessionToolbar = new QWidget(this);
    m_sessionToolbar->setObjectName(QStringLiteral("SessionToolbar"));
    m_sessionToolbar->hide();
    auto* toolbarLayout = new QHBoxLayout(m_sessionToolbar);
    toolbarLayout->setContentsMargins(12, 8, 12, 8);
    toolbarLayout->setSpacing(8);

    auto addToolBtn = [&](const QString& icon, const QString& label, bool danger = false) {
        auto* btn = new QPushButton(label, m_sessionToolbar);
        btn->setObjectName(danger ? QStringLiteral("ToolbarBtnDanger") : QStringLiteral("ToolbarBtn"));
        btn->setIcon(DarpanIcons::icon(icon));
        btn->setFlat(true);
        toolbarLayout->addWidget(btn);
        return btn;
        };

    addToolBtn(QStringLiteral("analytics"), tr("Stats"));
    addToolBtn(QStringLiteral("attach_file"), tr("Send File"));
    addToolBtn(QStringLiteral("fullscreen"), tr("Fullscreen"));
    auto* disconnectBtn = addToolBtn(QStringLiteral("power_settings_new"), tr("End"), true);
    QObject::connect(disconnectBtn, &QPushButton::clicked,
        this, &ViewerPage::disconnectRequested);
    // F11 fullscreen toggle
    m_fullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    QObject::connect(m_fullscreenShortcut, &QShortcut::activated,
        this, &ViewerPage::toggleFullscreen);

    m_fpsTimer.start();

    m_waitOverlay->setGeometry(rect());
    m_waitOverlay->show();
    m_waitOverlay->raise();
}

void ViewerPage::updateFrame(const QImage& frame)
{
    if (!m_renderer->isVisible()) {
        m_waitOverlay->hide();
        m_roleBadge->show();
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

void ViewerPage::onFrameDelivered()
{
    if (!m_renderer->isVisible()) {
        m_waitOverlay->hide();
        m_roleBadge->show();
        m_renderer->show();
    }
    // Reposition HUD (dimensions may have changed).
    repositionOverlays();

    // FPS tracking.
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
    m_roleBadge->hide();
    m_waitTitle->setText(message);
    m_waitOverlay->setGeometry(rect());
    m_waitOverlay->show();
    m_waitOverlay->raise();
    if (!m_roomId.isEmpty()) {
        m_roomIdFooter->setText(
            QStringLiteral("  %1  ").arg(m_roomId.toUpper()));
        m_roomIdFooter->show();
    }
}

void ViewerPage::hideWaitingOverlay()
{
    m_waitOverlay->hide();
    m_roleBadge->show();
    m_renderer->show();
}

void ViewerPage::setSessionRole(const QString& role)
{
    m_sessionRole = role;
    m_connInfo.role = role;

    const bool controlling = role == QLatin1String("controller");
    const QColor accent = QColor(controlling ? DarpanTheme::kRoleController : DarpanTheme::kRoleViewer);
    const QString label = controlling ? tr("CONTROLLING") : tr("VIEWING");
    const QString iconName = controlling ? QStringLiteral("gamepad") : QStringLiteral("visibility");

    m_roleBadge->setText(label);
    m_roleBadge->setStyleSheet(
        QStringLiteral("color: %1; border: 1px solid %1; background: rgba(33,38,45,0.92); border-radius: 14px; padding: 4px 10px;")
        .arg(accent.name()));
    Q_UNUSED(iconName);

    m_sessionToolbar->setVisible(controlling);
    repositionOverlays();
}

void ViewerPage::setRoomId(const QString& roomId)
{
    m_roomId = roomId;
    if (!roomId.isEmpty() && m_waitOverlay->isVisible()) {
        m_roomIdFooter->setText(QStringLiteral("  %1  ").arg(roomId.toUpper()));
        m_roomIdFooter->show();
    }
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
    const QRect cr = m_renderer->contentRect();

    m_waitOverlay->setGeometry(cr);

    if (m_roomIdFooter) {
        m_roomIdFooter->setGeometry(0, 0, 0, 0);
    }

    const int hudW = 420;
    const int hudH = 28;
    m_hud->setGeometry(cr.right() - hudW - 8, cr.top() + 8, hudW, hudH);

    if (m_roleBadge && m_roleBadge->isVisible()) {
        const int bw = qMax(120, m_roleBadge->sizeHint().width() + 24);
        m_roleBadge->setGeometry(cr.left() + 8, cr.top() + 8, bw, 28);
    }

    if (m_sessionToolbar && m_sessionToolbar->isVisible()) {
        const int tw = 520;
        const int th = 52;
        m_sessionToolbar->setGeometry(
            cr.center().x() - tw / 2, cr.bottom() - th - 16, tw, th);
        m_sessionToolbar->raise();
    }
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
}

void ViewerPage::mouseDoubleClickEvent(QMouseEvent* ev)
{
    emit mouseEvent(buildMouseData(ev, QStringLiteral("dblclick")));
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
    case Qt::Key_Left:   return QStringLiteral("ArrowLeft");
    case Qt::Key_Right:     return QStringLiteral("ArrowRight");
    case Qt::Key_Up:        return QStringLiteral("ArrowUp");
    case Qt::Key_Down:      return QStringLiteral("ArrowDown");
    case Qt::Key_Home:      return QStringLiteral("Home");
    case Qt::Key_End:       return QStringLiteral("End");
    case Qt::Key_PageUp:    return QStringLiteral("PageUp");
    case Qt::Key_PageDown:  return QStringLiteral("PageDown");
    case Qt::Key_Insert:    return QStringLiteral("Insert");
    case Qt::Key_F1:      return QStringLiteral("F1");
    case Qt::Key_F2:        return QStringLiteral("F2");
    case Qt::Key_F3:        return QStringLiteral("F3");
    case Qt::Key_F4:        return QStringLiteral("F4");
    case Qt::Key_F5:    return QStringLiteral("F5");
    case Qt::Key_F6:        return QStringLiteral("F6");
    case Qt::Key_F7:   return QStringLiteral("F7");
    case Qt::Key_F8:        return QStringLiteral("F8");
    case Qt::Key_F9:        return QStringLiteral("F9");
    case Qt::Key_F10:       return QStringLiteral("F10");
    case Qt::Key_F11:       return QStringLiteral("F11");
    case Qt::Key_F12:       return QStringLiteral("F12");
    case Qt::Key_Control:   return QStringLiteral("Control");
    case Qt::Key_Shift:     return QStringLiteral("Shift");
    case Qt::Key_Alt:     return QStringLiteral("Alt");
    case Qt::Key_Meta:      return QStringLiteral("Meta");
    case Qt::Key_CapsLock:  return QStringLiteral("CapsLock");
    case Qt::Key_NumLock:   return QStringLiteral("NumLock");
    case Qt::Key_ScrollLock:return QStringLiteral("ScrollLock");
    default: break;
    }

    // Fallback: letter keys (A-Z) – when Ctrl/Alt is held, Qt suppresses the
    // printable text and only gives us the key code. Convert Qt::Key_A..Z to
    // lowercase "a".."z" which the InputInjector VK table understands.
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar(QLatin1Char('a' + (key - Qt::Key_A))));
    }

    // Digit keys with modifiers held (text may be empty)
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar(QLatin1Char('0' + (key - Qt::Key_0))));
    }

    return QString();
}

void ViewerPage::keyPressEvent(QKeyEvent* ev)
{
    const QString name = qtKeyToName(ev->key(), ev->text());
    if (name.isEmpty()) { return; }

    KeyboardData d;
    d.key = name;
    d.type = QStringLiteral("keydown");
    // Do NOT populate d.modifiers. Each modifier key (Ctrl, Shift, Alt, Meta)
    // already fires its own separate keydown/keyup event. Including them here
    // causes the host to double-inject the modifier, triggering shortcuts.
    emit keyboardEvent(d);
}

void ViewerPage::keyReleaseEvent(QKeyEvent* ev)
{
    const QString name = qtKeyToName(ev->key(), ev->text());
    if (name.isEmpty()) { return; }

    KeyboardData d;
    d.key = name;
    d.type = QStringLiteral("keyup");
    // Same reasoning as keyPressEvent – no modifiers array.
    emit keyboardEvent(d);
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void ViewerPage::contextMenuEvent(QContextMenuEvent* ev)
{
    QMenu menu(this);

    QAction* screenshotAct = menu.addAction(tr("Take Screenshot"));
    QAction* fullscreenAct = menu.addAction(tr("Toggle Fullscreen\tF11"));
    menu.addSeparator();
    QAction* disconnectAct = menu.addAction(tr("Disconnect"));

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
