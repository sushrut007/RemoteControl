#include "AppShell.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QFile>
#include <QStyle>
#include <QIcon>
#include <QScreen>
#include <QActionGroup>
#include <QTimer>
#include <QSizeGrip>
#include <QDebug>

// ---------------------------------------------------------------------------
// Colours / sizes
// ---------------------------------------------------------------------------

static constexpr int k_topBarHeight = 48;
static constexpr int k_dotSize = 12;
static constexpr int k_fadeMs = 180;
static constexpr int k_defaultW = 1280;
static constexpr int k_defaultH = 800;

static const char k_bgColor[] = "#1a1a2e";
static const char k_topBarColor[] = "#16213e";
static const char k_dotConnected[] = "#00c853";
static const char k_dotConnecting[] = "#ffd600";
static const char k_dotDisconnected[] = "#d50000";

// ---------------------------------------------------------------------------
// ModalOverlay
// ---------------------------------------------------------------------------

ModalOverlay::ModalOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAutoFillBackground(false);
    // Always fill the parent — install an event filter so we track parent resizes.
    if (parent) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    hide();
}

void ModalOverlay::setContent(QWidget* content)
{
    clearContent();
    m_content = content;
    if (m_content) {
        m_content->setParent(this);
        m_content->show();
    }
    // Fill parent before showing so centering uses the correct size.
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
    recenter();
}

void ModalOverlay::clearContent()
{
    if (m_content) {
        m_content->hide();
        m_content->setParent(nullptr);
        m_content = nullptr;
    }
    hide();
}

void ModalOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recenter();
}

bool ModalOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void ModalOverlay::recenter()
{
    if (!m_content) return;
    // Prefer sizeHint; fall back to minimumSize (covers QDialog::setFixedSize),
    // then actual size.
    QSize cs = m_content->sizeHint();
    if (!cs.isValid() || cs.isEmpty())
        cs = m_content->minimumSize();
    if (!cs.isValid() || cs.isEmpty())
        cs = m_content->size();
    const int x = (width() - cs.width()) / 2;
    const int y = (height() - cs.height()) / 2;
    m_content->setGeometry(x, y, cs.width(), cs.height());
}

void ModalOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 160)); // semi-transparent scrim
}

void ModalOverlay::mousePressEvent(QMouseEvent* /*event*/)
{
    // Absorb mouse presses so they don't reach the content behind the overlay.
}

// ---------------------------------------------------------------------------
// AppShell – construction
// ---------------------------------------------------------------------------

AppShell::AppShell(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(k_defaultW, k_defaultH);

    buildUi();
    buildTray();
    loadStyleSheet();
}

AppShell::~AppShell() = default;

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void AppShell::buildUi()
{
    // -----------------------------------------------------------------------
    // Root widget
    // -----------------------------------------------------------------------
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("AppRoot"));
    setCentralWidget(root);

    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // -----------------------------------------------------------------------
    // Top bar
    // -----------------------------------------------------------------------
    m_topBar = new QWidget(root);
    m_topBar->setObjectName(QStringLiteral("TopBar"));
    m_topBar->setFixedHeight(k_topBarHeight);

    auto* topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(16, 0, 16, 0);
    topLayout->setSpacing(10);

    // Logo
    m_logoLabel = new QLabel(QStringLiteral("RemoteControl"), m_topBar);
    m_logoLabel->setObjectName(QStringLiteral("LogoLabel"));

    // Spacer
    topLayout->addWidget(m_logoLabel);
    topLayout->addStretch();

    // Connection status dot
    m_statusDot = new QLabel(m_topBar);
    m_statusDot->setObjectName(QStringLiteral("StatusDot"));
    m_statusDot->setFixedSize(k_dotSize, k_dotSize);
    // Start as disconnected (red)
    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:%2px;")
        .arg(QLatin1String(k_dotDisconnected))
        .arg(k_dotSize / 2));
    topLayout->addWidget(m_statusDot);

    // Settings button
    m_settingsBtn = new QPushButton(QStringLiteral("⚙"), m_topBar);
    m_settingsBtn->setObjectName(QStringLiteral("SettingsBtn"));
    m_settingsBtn->setFixedSize(32, 32);
    m_settingsBtn->setFlat(true);
    QObject::connect(m_settingsBtn, &QPushButton::clicked,
        this, &AppShell::settingsRequested);
    topLayout->addWidget(m_settingsBtn);

    // Minimize button
    m_minimizeBtn = new QPushButton(QStringLiteral("—"), m_topBar);
    m_minimizeBtn->setObjectName(QStringLiteral("MinimizeBtn"));
    m_minimizeBtn->setFixedSize(32, 32);
    m_minimizeBtn->setFlat(true);
    QObject::connect(m_minimizeBtn, &QPushButton::clicked,
        this, &QWidget::showMinimized);
    topLayout->addWidget(m_minimizeBtn);

    // Maximize / restore button
    m_maximizeBtn = new QPushButton(QStringLiteral("□"), m_topBar);
    m_maximizeBtn->setObjectName(QStringLiteral("MaximizeBtn"));
    m_maximizeBtn->setFixedSize(32, 32);
    m_maximizeBtn->setFlat(true);
    QObject::connect(m_maximizeBtn, &QPushButton::clicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
        });
    topLayout->addWidget(m_maximizeBtn);

    // Close button
    m_closeBtn = new QPushButton(QStringLiteral("✕"), m_topBar);
    m_closeBtn->setObjectName(QStringLiteral("CloseBtn"));
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setFlat(true);
    QObject::connect(m_closeBtn, &QPushButton::clicked,
        this, &AppShell::quitRequested);
    topLayout->addWidget(m_closeBtn);

    rootLayout->addWidget(m_topBar);

    // -----------------------------------------------------------------------
    // Page stack
    // -----------------------------------------------------------------------
    m_stack = new QStackedWidget(root);
    m_stack->setObjectName(QStringLiteral("PageStack"));

    // Placeholder pages – replace with real page widgets before showing.
    m_connectPage = new QWidget();
    m_connectPage->setObjectName(QStringLiteral("ConnectPage"));
    {
        // Show a helpful hint rather than a blank page when the modal is closed.
        auto* l = new QVBoxLayout(m_connectPage);
        l->setAlignment(Qt::AlignCenter);
        auto* icon = new QLabel(QStringLiteral("🖥"), m_connectPage);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral("font-size: 64px;"));
        auto* heading = new QLabel(QStringLiteral("RemoteControl"), m_connectPage);
        heading->setAlignment(Qt::AlignCenter);
        heading->setStyleSheet(QStringLiteral(
            "color: #e0e0ff; font-size: 24px; font-weight: bold; margin-top: 12px;"));
        auto* sub = new QLabel(
            QStringLiteral("Click ⚙ Settings or press Connect to start a session."),
            m_connectPage);
        sub->setAlignment(Qt::AlignCenter);
        sub->setWordWrap(true);
        sub->setStyleSheet(QStringLiteral("color: #6666aa; font-size: 13px; margin-top: 8px;"));
        auto* reconnectBtn = new QPushButton(QStringLiteral("Connect to Room"), m_connectPage);
        reconnectBtn->setObjectName(QStringLiteral("ConnectPageBtn"));
        reconnectBtn->setFixedHeight(40);
        reconnectBtn->setFixedWidth(180);
        reconnectBtn->setStyleSheet(QStringLiteral(
            "#ConnectPageBtn {"
            "  background: #6c63ff; color: #fff; border: none;"
            "  border-radius: 8px; font-size: 14px; font-weight: bold; margin-top: 20px;"
            "}"
            "#ConnectPageBtn:hover   { background: #7d75ff; }"
            "#ConnectPageBtn:pressed { background: #5548e0; }"));
        QObject::connect(reconnectBtn, &QPushButton::clicked,
            this, &AppShell::disconnectRequested);
        l->addWidget(icon);
        l->addWidget(heading);
        l->addWidget(sub);
        l->addSpacing(4);
        l->addWidget(reconnectBtn, 0, Qt::AlignHCenter);
    }

    m_viewerPage = new QWidget();
    m_viewerPage->setObjectName(QStringLiteral("ViewerPage"));
    {
        auto* lbl = new QLabel(QStringLiteral("Viewer Page"), m_viewerPage);
        lbl->setAlignment(Qt::AlignCenter);
        auto* l = new QVBoxLayout(m_viewerPage);
        l->addWidget(lbl);
    }

    m_hostPage = new QWidget();
    m_hostPage->setObjectName(QStringLiteral("HostPage"));
    {
        auto* lbl = new QLabel(QStringLiteral("Host Page"), m_hostPage);
        lbl->setAlignment(Qt::AlignCenter);
        auto* l = new QVBoxLayout(m_hostPage);
        l->addWidget(lbl);
    }

    m_stack->addWidget(m_connectPage);   // index 0
    m_stack->addWidget(m_viewerPage);    // index 1
    m_stack->addWidget(m_hostPage);      // index 2
    m_stack->setCurrentIndex(0);

    // Opacity effect + animation attached to the stack
    m_fadeEffect = new QGraphicsOpacityEffect(m_stack);
    m_fadeEffect->setOpacity(1.0);
    m_stack->setGraphicsEffect(m_fadeEffect);

    m_fadeAnim = new QPropertyAnimation(m_fadeEffect, "opacity", this);
    m_fadeAnim->setDuration(k_fadeMs);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);

    rootLayout->addWidget(m_stack, 1);

    // -----------------------------------------------------------------------
    // Modal overlay (full-window, raised above everything)
    // -----------------------------------------------------------------------
    m_overlay = new ModalOverlay(root);
    m_overlay->setGeometry(0, 0, width(), height());

    // Size grip for manual resizing on the frameless window.
    m_sizeGrip = new QSizeGrip(root);
    m_sizeGrip->setFixedSize(16, 16);
    m_sizeGrip->raise();
}

void AppShell::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayMenu = new QMenu(this);

    auto* showAction = m_trayMenu->addAction(QStringLiteral("Show"));
    m_trayMenu->addSeparator();
    auto* disconnectAction = m_trayMenu->addAction(QStringLiteral("Disconnect"));
    m_trayMenu->addSeparator();
    auto* quitAction = m_trayMenu->addAction(QStringLiteral("Quit"));

    QObject::connect(showAction, &QAction::triggered, this, &QWidget::showNormal);
    QObject::connect(disconnectAction, &QAction::triggered, this, &AppShell::disconnectRequested);
    QObject::connect(quitAction, &QAction::triggered, this, &AppShell::quitRequested);

    m_trayIcon = new QSystemTrayIcon(this);
    // Use the application icon; fall back to a generic window icon.
    QIcon icon = QApplication::windowIcon();
    if (icon.isNull()) {
        icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(QStringLiteral("RemoteControl"));
    m_trayIcon->setContextMenu(m_trayMenu);

    QObject::connect(m_trayIcon, &QSystemTrayIcon::activated,
        this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick ||
                reason == QSystemTrayIcon::Trigger) {
                showNormal();
                raise();
                activateWindow();
            }
        });

    m_trayIcon->show();
}

void AppShell::loadStyleSheet()
{
    QFile qss(QStringLiteral(":/styles/dark.qss"));
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        const QString style = QString::fromUtf8(qss.readAll());
        setStyleSheet(style);
    }
    else {
        // Inline minimal fallback so the window looks correct even without the
        // resource file compiled in.
        setStyleSheet(QStringLiteral(R"(
            #AppRoot {
                background: #1a1a2e;
            }
            #TopBar {
                background: #16213e;
                border-bottom: 1px solid #0f3460;
            }
            #LogoLabel {
                color: #e0e0e0;
                font-size: 15px;
                font-weight: bold;
                letter-spacing: 1px;
            }
            #SettingsBtn {
                color: #a0a0c0;
                font-size: 18px;
                border: none;
                background: transparent;
            }
            #SettingsBtn:hover {
                color: #ffffff;
            }
            #MinimizeBtn {
                color: #a0a0c0;
                font-size: 14px;
                border: none;
                background: transparent;
            }
            #MinimizeBtn:hover {
                color: #ffffff;
            }
            #CloseBtn {
                color: #a0a0c0;
                font-size: 14px;
                border: none;
                background: transparent;
            }
            #CloseBtn:hover {
                color: #ffffff;
                background: #c0392b;
            }
            #PageStack {
                background: #1a1a2e;
            }
            QLabel {
                color: #e0e0e0;
            }
        )"));
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void AppShell::showPage(PageType page)
{
    int targetIndex = 0;
    switch (page) {
    case PageType::Connect: targetIndex = 0; break;
    case PageType::Viewer:  targetIndex = 1; break;
    case PageType::Host:    targetIndex = 2; break;
    }

    if (m_stack->currentIndex() == targetIndex) {
        return;
    }

    applyFadeTransition(m_stack->currentWidget(),
        m_stack->widget(targetIndex));

    // Switch page at the midpoint of the fade (when opacity == 0).
    QObject::connect(m_fadeAnim, &QPropertyAnimation::finished,
        this, [this, targetIndex]() {
            m_stack->setCurrentIndex(targetIndex);
            // Fade back in
            m_fadeAnim->disconnect();
            m_fadeAnim->setStartValue(0.0);
            m_fadeAnim->setEndValue(1.0);
            m_fadeAnim->start();
        }, Qt::SingleShotConnection);

    m_fadeAnim->setStartValue(1.0);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void AppShell::applyFadeTransition(QWidget* /*outgoing*/, QWidget* /*incoming*/)
{
    // Visual preparation hook – could pre-render the incoming page here.
}

void AppShell::setPageWidget(PageType page, QWidget* widget)
{
    // Map page type → stack index and current pointer reference
    int     index = 0;
    QWidget** stored = nullptr;

    switch (page) {
    case PageType::Connect:
        index = 0;
        stored = &m_connectPage;
        break;
    case PageType::Viewer:
        index = 1;
        stored = &m_viewerPage;
        break;
    case PageType::Host:
        index = 2;
        stored = &m_hostPage;
        break;
    }

    Q_ASSERT(stored);
    if (!widget || widget == *stored) { return; }

    // Remove old placeholder, insert real widget at the same index
    QWidget* old = m_stack->widget(index);
    m_stack->removeWidget(old);
    delete old;

    widget->setParent(m_stack);
    m_stack->insertWidget(index, widget);
    *stored = widget;
}

// ---------------------------------------------------------------------------
// Modal
// ---------------------------------------------------------------------------

void AppShell::showModal(QWidget* modal)
{
    m_overlay->setContent(modal);
}

void AppShell::hideModal()
{
    m_overlay->clearContent();
}

void AppShell::recenterModal()
{
    m_overlay->recenter();
}

// ---------------------------------------------------------------------------
// Connection status
// ---------------------------------------------------------------------------

void AppShell::setConnectionStatus(ConnectionStatus status)
{
    const char* color = k_dotDisconnected;
    QString tip;

    switch (status) {
    case ConnectionStatus::Connected:
        color = k_dotConnected;
        tip = QStringLiteral("Connected");
        break;
    case ConnectionStatus::Connecting:
        color = k_dotConnecting;
        tip = QStringLiteral("Connecting…");
        break;
    case ConnectionStatus::Disconnected:
        color = k_dotDisconnected;
        tip = QStringLiteral("Disconnected");
        break;
    }

    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:%2px;")
        .arg(QLatin1String(color))
        .arg(k_dotSize / 2));
    m_statusDot->setToolTip(tip);

    if (m_trayIcon) {
        m_trayIcon->setToolTip(QStringLiteral("RemoteControl – ") + tip);
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void AppShell::closeEvent(QCloseEvent* event)
{
    event->accept();
    emit quitRequested();
}

void AppShell::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_maximizeBtn) {
        m_maximizeBtn->setText(isMaximized() ? QStringLiteral("❐") : QStringLiteral("□"));
    }
}

void AppShell::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Keep the overlay covering the full central widget area.
    if (m_overlay && centralWidget()) {
        m_overlay->setGeometry(centralWidget()->rect());
        m_overlay->raise();
    }

    // Reposition size grip to bottom-right corner; hide when maximized.
    if (m_sizeGrip) {
        m_sizeGrip->setVisible(!isMaximized());
        if (!isMaximized() && centralWidget()) {
            const QRect r = centralWidget()->rect();
            m_sizeGrip->move(r.right() - m_sizeGrip->width(),
                r.bottom() - m_sizeGrip->height());
            m_sizeGrip->raise();
        }
    }

    // Update the maximize button icon to reflect current window state.
    if (m_maximizeBtn) {
        m_maximizeBtn->setText(isMaximized() ? QStringLiteral("❐") : QStringLiteral("□"));
    }
}

void AppShell::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_topBar &&
        !isMaximized() &&
        m_topBar->geometry().contains(event->pos())) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    else {
        QMainWindow::mousePressEvent(event);
    }
}

void AppShell::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_topBar && m_topBar->geometry().contains(event->pos())) {
        if (isMaximized()) showNormal(); else showMaximized();
        event->accept();
    }
    else {
        QMainWindow::mouseDoubleClickEvent(event);
    }
}

void AppShell::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
    }
    else {
        QMainWindow::mouseMoveEvent(event);
    }
}

void AppShell::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}