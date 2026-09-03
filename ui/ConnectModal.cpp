#include "ConnectModal.h"
#include "RoleSelectorWidget.h"
#include "DarpanIcons.h"
#include "DarpanTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QTextEdit>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QTime>

// ---------------------------------------------------------------------------
// SpinnerWidget
// ---------------------------------------------------------------------------

SpinnerWidget::SpinnerWidget(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setFixedSize(22, 22);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timer->setInterval(16);
    QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
        setAngle((m_angle + 6) % 360);
        });
    hide();
}

void SpinnerWidget::setAngle(int a)
{
    m_angle = a;
    update();
}

void SpinnerWidget::startSpinning()
{
    show();
    m_timer->start();
}

void SpinnerWidget::stopSpinning()
{
    m_timer->stop();
    hide();
}

void SpinnerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int  side = qMin(width(), height()) - 2;
    const QRectF arc(1, 1, side, side);
    QPen pen(QColor(DarpanTheme::kRoleController), 3, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawArc(arc, -m_angle * 16, 270 * 16);
}

// ---------------------------------------------------------------------------
// ConnectModal
// ---------------------------------------------------------------------------

static constexpr int k_modalW = 480;
static constexpr int k_radius = 14;

ConnectModal::ConnectModal(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumWidth(k_modalW);
    setMaximumWidth(k_modalW);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 120));
    setGraphicsEffect(shadow);

    buildUi();
    loadSettings();
}

void ConnectModal::buildUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("ModalCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // Header
    auto* header = new QWidget(card);
    header->setObjectName(QStringLiteral("ModalHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 16, 16, 16);
    auto* title = new QLabel(tr("Connect to Room"), header);
    title->setObjectName(QStringLiteral("ModalTitle"));
    m_closeBtn = new QPushButton(header);
    m_closeBtn->setObjectName(QStringLiteral("ModalCloseBtn"));
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setIcon(DarpanIcons::icon(QStringLiteral("close")));
    m_closeBtn->setFlat(true);
    QObject::connect(m_closeBtn, &QPushButton::clicked, this, &ConnectModal::onCancelClicked);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_closeBtn);
    cardLayout->addWidget(header);

    // Body
    auto* body = new QWidget(card);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(24, 16, 24, 16);
    bodyLayout->setSpacing(16);

    // Room ID
    {
        auto* lbl = new QLabel(tr("Room ID"), body);
        lbl->setObjectName(QStringLiteral("ModalLabel"));
        bodyLayout->addWidget(lbl);

        auto* row = new QWidget(body);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* icon = new QLabel(row);
        icon->setPixmap(DarpanIcons::tintedPixmap(
            QStringLiteral("meeting_room"), QColor(DarpanTheme::kTextMuted), QSize(18, 18)));
        icon->setFixedWidth(24);
        m_roomEdit = new QLineEdit(row);
        m_roomEdit->setObjectName(QStringLiteral("ModalInput"));
        m_roomEdit->setPlaceholderText(tr("Enter room ID"));
        rowLayout->addWidget(icon);
        rowLayout->addWidget(m_roomEdit, 1);
        bodyLayout->addWidget(row);
    }

    // Password
    {
        auto* lbl = new QLabel(tr("Password  (Optional)"), body);
        lbl->setObjectName(QStringLiteral("ModalLabel"));
        bodyLayout->addWidget(lbl);

        auto* row = new QWidget(body);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* icon = new QLabel(row);
        icon->setPixmap(DarpanIcons::tintedPixmap(
            QStringLiteral("lock"), QColor(DarpanTheme::kTextMuted), QSize(18, 18)));
        icon->setFixedWidth(24);
        m_passEdit = new QLineEdit(row);
        m_passEdit->setObjectName(QStringLiteral("ModalInput"));
        m_passEdit->setPlaceholderText(tr("Enter password"));
        m_passEdit->setEchoMode(QLineEdit::Password);
        m_passToggleBtn = new QPushButton(row);
        m_passToggleBtn->setObjectName(QStringLiteral("ModalPassToggle"));
        m_passToggleBtn->setFixedSize(28, 28);
        m_passToggleBtn->setIcon(DarpanIcons::icon(QStringLiteral("visibility")));
        m_passToggleBtn->setFlat(true);
        QObject::connect(m_passToggleBtn, &QPushButton::clicked, this, [this]() {
            const bool hidden = m_passEdit->echoMode() == QLineEdit::Password;
            m_passEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
            m_passToggleBtn->setIcon(DarpanIcons::icon(
                hidden ? QStringLiteral("visibility_off") : QStringLiteral("visibility")));
            });
        rowLayout->addWidget(icon);
        rowLayout->addWidget(m_passEdit, 1);
        rowLayout->addWidget(m_passToggleBtn);
        bodyLayout->addWidget(row);
    }

    // Role
    {
        auto* lbl = new QLabel(tr("Role"), body);
        lbl->setObjectName(QStringLiteral("ModalLabel"));
        bodyLayout->addWidget(lbl);
        m_roleSelector = new RoleSelectorWidget(body);
        bodyLayout->addWidget(m_roleSelector);
    }

    m_rememberCheck = new QCheckBox(tr("Remember room and role"), body);
    m_rememberCheck->setObjectName(QStringLiteral("ModalCheck"));
    bodyLayout->addWidget(m_rememberCheck);

    m_localServerCheck = new QCheckBox(tr("Connect to local server (http://localhost:5000)"), body);
    m_localServerCheck->setObjectName(QStringLiteral("ModalCheck"));
    bodyLayout->addWidget(m_localServerCheck);

    m_statusLabel = new QLabel(body);
    m_statusLabel->setObjectName(QStringLiteral("ModalStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    bodyLayout->addWidget(m_statusLabel);

    m_logPanel = new QTextEdit(body);
    m_logPanel->setObjectName(QStringLiteral("LogPanel"));
    m_logPanel->setReadOnly(true);
    m_logPanel->setFixedHeight(110);
    m_logPanel->setVisible(false);
    bodyLayout->addWidget(m_logPanel);

    cardLayout->addWidget(body);

    // Footer
    auto* footer = new QWidget(card);
    footer->setObjectName(QStringLiteral("ModalFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(24, 16, 24, 16);
    m_spinner = new SpinnerWidget(footer);
    footerLayout->addWidget(m_spinner);
    footerLayout->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("ModalCancelBtn"));
    m_cancelBtn->setFixedHeight(36);
    m_connectBtn = new QPushButton(tr("Connect"), footer);
    m_connectBtn->setObjectName(QStringLiteral("ModalConnectBtn"));
    m_connectBtn->setFixedHeight(36);
    m_connectBtn->setIcon(DarpanIcons::icon(
        QStringLiteral("arrow_forward"), QSize(16, 16), Qt::white));
    m_connectBtn->setDefault(true);
    footerLayout->addWidget(m_cancelBtn);
    footerLayout->addWidget(m_connectBtn);
    cardLayout->addWidget(footer);

    outerLayout->addWidget(card);

    QObject::connect(m_connectBtn, &QPushButton::clicked,
        this, &ConnectModal::onConnectClicked);
    QObject::connect(m_cancelBtn, &QPushButton::clicked,
        this, &ConnectModal::onCancelClicked);
}

void ConnectModal::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ConnectModal"));
    const bool remember = s.value(QStringLiteral("rememberMe"), false).toBool();
    if (remember) {
        m_roomEdit->setText(s.value(QStringLiteral("roomId")).toString());
        m_roleSelector->setSelectedRole(
            s.value(QStringLiteral("appType"), QStringLiteral("controller")).toString());
        m_rememberCheck->setChecked(true);
    }
    m_localServerCheck->setChecked(s.value(QStringLiteral("useLocalServer"), false).toBool());
    s.endGroup();
}

void ConnectModal::saveSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ConnectModal"));
    s.setValue(QStringLiteral("rememberMe"), m_rememberCheck->isChecked());
    s.setValue(QStringLiteral("useLocalServer"), m_localServerCheck->isChecked());
    if (m_rememberCheck->isChecked()) {
        s.setValue(QStringLiteral("roomId"), m_roomEdit->text().trimmed());
        s.setValue(QStringLiteral("appType"), m_roleSelector->selectedRole());
    }
    else {
        s.remove(QStringLiteral("roomId"));
        s.remove(QStringLiteral("appType"));
    }
    s.endGroup();
}

void ConnectModal::setConfig(const ConnectionConfig& cfg)
{
    m_roomEdit->setText(cfg.roomId);
    m_passEdit->setText(cfg.password);
    m_roleSelector->setSelectedRole(cfg.appType);
    m_rememberCheck->setChecked(cfg.rememberMe);
    if (cfg.serverUrl.contains(QStringLiteral("localhost")) || cfg.serverUrl.contains(QStringLiteral("127.0.0.1"))) {
        m_localServerCheck->setChecked(true);
    }
}

void ConnectModal::setStatusMessage(const QString& msg, bool isError)
{
    if (msg.isEmpty()) {
        m_statusLabel->hide();
        return;
    }
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(
        isError ? QStringLiteral("color:#FFB4AB; font-size:12px;")
        : QStringLiteral("color:#10B981; font-size:12px;"));
    m_statusLabel->show();
}

void ConnectModal::setLoading(bool loading)
{
    setInputsEnabled(!loading);
    m_connectBtn->setText(loading ? tr("Connecting…") : tr("Connect"));
    m_connectBtn->setEnabled(!loading);

    if (loading) {
        m_spinner->startSpinning();
        m_logPanel->setVisible(true);
        m_logPanel->clear();
    }
    else {
        m_spinner->stopSpinning();
    }
    adjustSize();
    emit sizeChanged();
}

void ConnectModal::onConnectClicked()
{
    if (!validate()) { return; }
    saveSettings();
    setStatusMessage(QString());
    emit connectRequested(currentConfig());
}

void ConnectModal::onCancelClicked()
{
    emit cancelled();
}

bool ConnectModal::validate()
{
    if (m_roomEdit->text().trimmed().isEmpty()) {
        setStatusMessage(tr("Room ID is required."));
        m_roomEdit->setFocus();
        return false;
    }
    return true;
}

ConnectionConfig ConnectModal::currentConfig() const
{
    ConnectionConfig cfg;
    if (m_localServerCheck && m_localServerCheck->isChecked()) {
        cfg.serverUrl = QStringLiteral("http://localhost:5000");
    } else {
        const AppSettings s = APP_STATE->appSettings();
        cfg.serverUrl = !s.serverUrl.isEmpty() ? s.serverUrl : QStringLiteral("https://remotecontrol.sushrutmakes.qzz.io");
    }
    cfg.roomId = m_roomEdit->text().trimmed();
    cfg.password = m_passEdit->text();
    cfg.appType = m_roleSelector->selectedRole();
    cfg.rememberMe = m_rememberCheck->isChecked();
    return cfg;
}

void ConnectModal::setInputsEnabled(bool enabled)
{
    m_roomEdit->setEnabled(enabled);
    m_passEdit->setEnabled(enabled);
    m_passToggleBtn->setEnabled(enabled);
    m_roleSelector->setEnabled(enabled);
    m_rememberCheck->setEnabled(enabled);
    m_localServerCheck->setEnabled(enabled);
    m_connectBtn->setEnabled(enabled);
    m_cancelBtn->setEnabled(enabled);
    m_closeBtn->setEnabled(enabled);
}

void ConnectModal::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), k_radius, k_radius);
    p.fillPath(path, QColor(0x16, 0x1B, 0x22));
}

void ConnectModal::appendLog(const QString& message)
{
    if (!m_logPanel) { return; }
    m_logPanel->setVisible(true);
    const QString ts = QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    m_logPanel->append(QStringLiteral("<span style='color:#484F58'>%1</span> %2")
        .arg(ts, message.toHtmlEscaped()));
    QTextCursor c = m_logPanel->textCursor();
    c.movePosition(QTextCursor::End);
    m_logPanel->setTextCursor(c);
}
