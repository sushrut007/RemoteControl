#include "AppShell.h"
#include "DarpanIcons.h"
#include "DarpanTheme.h"

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

static const char k_dotConnected[] = "#10B981";
static const char k_dotConnecting[] = "#F59E0B";
static const char k_dotDisconnected[] = "#484F58";

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

    int x = (width() - m_content->width()) / 2;
    int y = (height() - m_content->height()) / 2;
    m_content->move(x, y);
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

    m_logoLabel = new QLabel(QStringLiteral("Darpan"), m_topBar);
    m_logoLabel->setObjectName(QStringLiteral("LogoLabel"));

    m_sessionBreadcrumb = new QLabel(m_topBar);
    m_sessionBreadcrumb->setObjectName(QStringLiteral("SessionBreadcrumb"));
    m_sessionBreadcrumb->hide();

    topLayout->addWidget(m_logoLabel);
    topLayout->addSpacing(12);
    topLayout->addWidget(m_sessionBreadcrumb);
    topLayout->addStretch();

    m_statusPill = new QWidget(m_topBar);
    m_statusPill->setObjectName(QStringLiteral("StatusPill"));
    auto* pillLayout = new QHBoxLayout(m_statusPill);
    pillLayout->setContentsMargins(8, 2, 8, 2);
    pillLayout->setSpacing(6);
    m_statusDot = new QLabel(m_statusPill);
    m_statusDot->setObjectName(QStringLiteral("StatusDot"));
    m_statusDot->setFixedSize(8, 8);
    m_statusPillLabel = new QLabel(tr("Disconnected"), m_statusPill);
    m_statusPillLabel->setObjectName(QStringLiteral("StatusPillLabel"));
    pillLayout->addWidget(m_statusDot);
    pillLayout->addWidget(m_statusPillLabel);
    topLayout->addWidget(m_statusPill);

    m_settingsBtn = new QPushButton(m_topBar);
    m_settingsBtn->setObjectName(QStringLiteral("WinBtn"));
    m_settingsBtn->setFixedSize(32, 32);
    m_settingsBtn->setIcon(DarpanIcons::icon(QStringLiteral("settings")));
    m_settingsBtn->setToolTip(tr("Settings"));
    m_settingsBtn->setFlat(true);
    QObject::connect(m_settingsBtn, &QPushButton::clicked,
        this, &AppShell::settingsRequested);
    topLayout->addWidget(m_settingsBtn);

    m_minimizeBtn = new QPushButton(m_topBar);
    m_minimizeBtn->setObjectName(QStringLiteral("WinBtn"));
    m_minimizeBtn->setFixedSize(32, 32);
    m_minimizeBtn->setIcon(DarpanIcons::icon(QStringLiteral("minimize")));
    m_minimizeBtn->setFlat(true);
    QObject::connect(m_minimizeBtn, &QPushButton::clicked,
        this, &QWidget::showMinimized);
    topLayout->addWidget(m_minimizeBtn);

    m_maximizeBtn = new QPushButton(m_topBar);
    m_maximizeBtn->setObjectName(QStringLiteral("WinBtn"));
    m_maximizeBtn->setFixedSize(32, 32);
    m_maximizeBtn->setIcon(DarpanIcons::icon(QStringLiteral("fullscreen")));
    m_maximizeBtn->setFlat(true);
    QObject::connect(m_maximizeBtn, &QPushButton::clicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
        });
    topLayout->addWidget(m_maximizeBtn);

    m_closeBtn = new QPushButton(m_topBar);
    m_closeBtn->setObjectName(QStringLiteral("CloseBtn"));
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setIcon(DarpanIcons::icon(QStringLiteral("close")));
    m_closeBtn->setFlat(true);
    QObject::connect(m_closeBtn, &QPushButton::clicked,
        this, &AppShell::quitRequested);
    topLayout->addWidget(m_closeBtn);

    setConnectionStatus(ConnectionStatus::Disconnected);

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
        auto* l = new QVBoxLayout(m_connectPage);
        l->setAlignment(Qt::AlignCenter);
        l->setSpacing(16);

        auto* iconFrame = new QLabel(m_connectPage);
        iconFrame->setObjectName(QStringLiteral("LandingIconFrame"));
        iconFrame->setFixedSize(128, 128);
        iconFrame->setAlignment(Qt::AlignCenter);
        iconFrame->setPixmap(DarpanIcons::tintedPixmap(
            QStringLiteral("cast"), QColor(DarpanTheme::kRoleController), QSize(48, 48)));

        auto* heading = new QLabel(tr("Remote access made simple"), m_connectPage);
        heading->setObjectName(QStringLiteral("LandingTitle"));
        heading->setAlignment(Qt::AlignCenter);

        auto* sub = new QLabel(
            tr("Secure rooms, low latency, and cross-device control for everyone."),
            m_connectPage);
        sub->setObjectName(QStringLiteral("LandingSubtitle"));
        sub->setAlignment(Qt::AlignCenter);
        sub->setWordWrap(true);
        sub->setMaximumWidth(420);

        auto* reconnectBtn = new QPushButton(tr("Connect to Room"), m_connectPage);
        reconnectBtn->setObjectName(QStringLiteral("ConnectPageBtn"));
        reconnectBtn->setIcon(DarpanIcons::icon(
            QStringLiteral("login"), QSize(20, 20), Qt::white));
        reconnectBtn->setFixedHeight(48);
        reconnectBtn->setMinimumWidth(280);
        QObject::connect(reconnectBtn, &QPushButton::clicked,
            this, &AppShell::disconnectRequested);

        auto* settingsLink = new QPushButton(tr("Settings"), m_connectPage);
        settingsLink->setObjectName(QStringLiteral("SettingsLinkBtn"));
        settingsLink->setIcon(DarpanIcons::icon(QStringLiteral("settings"), QSize(16, 16)));
        settingsLink->setFlat(true);
        QObject::connect(settingsLink, &QPushButton::clicked,
            this, &AppShell::settingsRequested);

        l->addStretch(1);
        l->addWidget(iconFrame, 0, Qt::AlignHCenter);
        l->addWidget(heading);
        l->addWidget(sub);
        l->addSpacing(8);
        l->addWidget(reconnectBtn, 0, Qt::AlignHCenter);
        l->addWidget(settingsLink, 0, Qt::AlignHCenter);
        l->addStretch(2);
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
    m_trayIcon->setToolTip(QStringLiteral("Darpan"));
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
    QString label = tr("Disconnected");

    switch (status) {
    case ConnectionStatus::Connected:
        color = k_dotConnected;
        label = tr("Connected");
        break;
    case ConnectionStatus::Connecting:
        color = k_dotConnecting;
        label = tr("Connecting…");
        break;
    case ConnectionStatus::Disconnected:
        color = k_dotDisconnected;
        label = tr("Disconnected");
        break;
    }

    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:4px;")
        .arg(QLatin1String(color)));
    m_statusPillLabel->setText(label);
    m_statusDot->setToolTip(label);

    if (m_trayIcon) {
        m_trayIcon->setToolTip(QStringLiteral("Darpan – ") + label);
    }
}

void AppShell::setSessionInfo(const QString& roomId, const QString& role)
{
    if (roomId.isEmpty()) {
        m_sessionBreadcrumb->hide();
        return;
    }
    const QString roleLabel = role.isEmpty()
        ? QString()
        : role.at(0).toUpper() + role.mid(1);
    m_sessionBreadcrumb->setText(
        QStringLiteral("Room: %1%2%3")
        .arg(roomId,
            roleLabel.isEmpty() ? QString() : QStringLiteral(" • "),
            roleLabel));
    m_sessionBreadcrumb->show();
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