#include "ConnectModal.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QUrl>

// ---------------------------------------------------------------------------
// SpinnerWidget
// ---------------------------------------------------------------------------

SpinnerWidget::SpinnerWidget(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setFixedSize(22, 22);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timer->setInterval(16); // ~60 fps
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
    const int  span = 270 * 16;
    const int  start = -m_angle * 16;

    QPen pen(QColor(0x6c, 0x63, 0xff), 3, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawArc(arc, start, span);
}

// ---------------------------------------------------------------------------
// ConnectModal
// ---------------------------------------------------------------------------

static constexpr int k_modalW = 480;
static constexpr int k_modalH = 340;
static constexpr int k_radius = 14;

ConnectModal::ConnectModal(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // Fixed width; height grows when the log panel appears.
    setMinimumWidth(k_modalW);
    setMaximumWidth(k_modalW);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // Drop shadow on the whole dialog
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 120));
    setGraphicsEffect(shadow);

    buildUi();
    loadSettings();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void ConnectModal::buildUi()
{
    // Outer layout provides the 16 px padding inside the rounded card.
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(0);

    // ── Title ────────────────────────────────────────────────────────────
    auto* title = new QLabel(QStringLiteral("Connect to Room"), this);
    title->setObjectName(QStringLiteral("ModalTitle"));
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    outerLayout->addWidget(title);
    outerLayout->addSpacing(12);

    // ── Form ─────────────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_urlEdit = new QLineEdit(QStringLiteral("ws://localhost:3000"), this);
    m_urlEdit->setObjectName(QStringLiteral("ModalInput"));
    m_urlEdit->setPlaceholderText(QStringLiteral("ws://host:port"));

    m_roomEdit = new QLineEdit(this);
    m_roomEdit->setObjectName(QStringLiteral("ModalInput"));
    m_roomEdit->setPlaceholderText(QStringLiteral("Enter room ID"));

    m_passEdit = new QLineEdit(this);
    m_passEdit->setObjectName(QStringLiteral("ModalInput"));
    m_passEdit->setPlaceholderText(QStringLiteral("Optional password"));
    m_passEdit->setEchoMode(QLineEdit::Password);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->setObjectName(QStringLiteral("ModalCombo"));
    m_typeCombo->addItem(QStringLiteral("Host"), QStringLiteral("host"));
    m_typeCombo->addItem(QStringLiteral("Viewer"), QStringLiteral("viewer"));
    m_typeCombo->addItem(QStringLiteral("Controller"), QStringLiteral("controller"));

    auto* urlLabel = new QLabel(QStringLiteral("Server URL"), this);
    auto* roomLabel = new QLabel(QStringLiteral("Room ID"), this);
    auto* passLabel = new QLabel(QStringLiteral("Password"), this);
    auto* typeLabel = new QLabel(QStringLiteral("App Type"), this);
    for (auto* lbl : { urlLabel, roomLabel, passLabel, typeLabel }) {
        lbl->setObjectName(QStringLiteral("ModalLabel"));
    }

    form->addRow(urlLabel, m_urlEdit);
    form->addRow(roomLabel, m_roomEdit);
    form->addRow(passLabel, m_passEdit);
    form->addRow(typeLabel, m_typeCombo);

    outerLayout->addLayout(form);
    outerLayout->addSpacing(8);

    // ── Remember me ──────────────────────────────────────────────────────
    m_rememberCheck = new QCheckBox(QStringLiteral("Remember me"), this);
    m_rememberCheck->setObjectName(QStringLiteral("ModalCheck"));
    outerLayout->addWidget(m_rememberCheck);
    outerLayout->addSpacing(6);

    // ── Status label ─────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("ModalStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    outerLayout->addWidget(m_statusLabel);

    outerLayout->addStretch();

    // ── Connection log ────────────────────────────────────────────────────
    m_logPanel = new QTextEdit(this);
    m_logPanel->setObjectName(QStringLiteral("LogPanel"));
    m_logPanel->setReadOnly(true);
    m_logPanel->setFixedHeight(110);
    m_logPanel->setPlaceholderText(tr("Connection log…"));
    m_logPanel->setVisible(false);   // hidden until loading starts
    outerLayout->addWidget(m_logPanel);
    // ── Buttons row ───────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_spinner = new SpinnerWidget(this);
    btnRow->addWidget(m_spinner);
    btnRow->addStretch();

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelBtn->setObjectName(QStringLiteral("ModalCancelBtn"));
    m_cancelBtn->setFixedHeight(36);

    m_connectBtn = new QPushButton(QStringLiteral("Connect"), this);
    m_connectBtn->setObjectName(QStringLiteral("ModalConnectBtn"));
    m_connectBtn->setFixedHeight(36);
    m_connectBtn->setDefault(true);

    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_connectBtn);

    outerLayout->addLayout(btnRow);

    // ── Signals ──────────────────────────────────────────────────────────
    QObject::connect(m_connectBtn, &QPushButton::clicked,
        this, &ConnectModal::onConnectClicked);
    QObject::connect(m_cancelBtn, &QPushButton::clicked,
        this, &ConnectModal::onCancelClicked);

    // ── Stylesheet ───────────────────────────────────────────────────────
    setStyleSheet(QStringLiteral(R"(
        #ModalTitle {
            color: #e2e2f0;
            font-size: 17px;
            font-weight: bold;
        }
        #ModalLabel {
            color: #9999bb;
            font-size: 12px;
        }
        #ModalInput {
            background: #0f2040;
            color: #e0e0ff;
            border: 1px solid #2a2a6e;
            border-radius: 7px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #5050c8;
        }
        #ModalInput:focus { border-color: #6c63ff; }
        #ModalCombo {
            background: #0f2040;
            color: #e0e0ff;
            border: 1px solid #2a2a6e;
            border-radius: 7px;
            padding: 5px 10px;
            font-size: 13px;
        }
        #ModalCombo::drop-down { border: none; width: 22px; }
        #ModalCombo QAbstractItemView {
            background: #16213e;
            color: #dcdcf0;
            border: 1px solid #2a2a5e;
            selection-background-color: #2a2a6e;
        }
        #ModalCheck {
            color: #aaaacc;
            font-size: 12px;
        }
        #ModalStatus {
            font-size: 12px;
            padding: 2px 4px;
        }
        #ModalCancelBtn {
            background: #1a1a3e;
            color: #aaaacc;
            border: 1px solid #2a2a5e;
            border-radius: 8px;
            padding: 0 20px;
            font-size: 13px;
        }
        #ModalCancelBtn:hover  { background: #2a2a5e; color: #ffffff; }
        #ModalCancelBtn:pressed { background: #111130; }
        #ModalConnectBtn {
            background: #6c63ff;
            color: #ffffff;
            border: none;
            border-radius: 8px;
            padding: 0 24px;
            font-size: 13px;
            font-weight: bold;
        }
        #ModalConnectBtn:hover   { background: #7d75ff; }
        #ModalConnectBtn:pressed { background: #5548e0; }
        #ModalConnectBtn:disabled {
            background: #3a3070;
            color: #888899;
        }
        #LogPanel {
            background: #0a0e1a;
            color: #7788cc;
            border: 1px solid #1e1e4e;
            border-radius: 6px;
            font-family: Consolas, monospace;
            font-size: 11px;
            padding: 4px;
        }
    )"));
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------

void ConnectModal::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ConnectModal"));
    const bool remember = s.value(QStringLiteral("rememberMe"), false).toBool();
    if (remember) {
        m_urlEdit->setText(
            s.value(QStringLiteral("serverUrl"),
                QStringLiteral("ws://localhost:3000")).toString());
        m_roomEdit->setText(s.value(QStringLiteral("roomId")).toString());
        const int typeIdx = s.value(QStringLiteral("appTypeIndex"), 0).toInt();
        m_typeCombo->setCurrentIndex(
            qBound(0, typeIdx, m_typeCombo->count() - 1));
        m_rememberCheck->setChecked(true);
    }
    s.endGroup();
}

void ConnectModal::saveSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ConnectModal"));
    s.setValue(QStringLiteral("rememberMe"), m_rememberCheck->isChecked());
    if (m_rememberCheck->isChecked()) {
        s.setValue(QStringLiteral("serverUrl"), m_urlEdit->text().trimmed());
        s.setValue(QStringLiteral("roomId"), m_roomEdit->text().trimmed());
        s.setValue(QStringLiteral("appTypeIndex"), m_typeCombo->currentIndex());
    }
    else {
        s.remove(QStringLiteral("serverUrl"));
        s.remove(QStringLiteral("roomId"));
        s.remove(QStringLiteral("appTypeIndex"));
    }
    s.endGroup();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ConnectModal::setConfig(const ConnectionConfig& cfg)
{
    m_urlEdit->setText(cfg.serverUrl);
    m_roomEdit->setText(cfg.roomId);
    m_passEdit->setText(cfg.password);
    const int idx = m_typeCombo->findData(cfg.appType);
    if (idx >= 0) { m_typeCombo->setCurrentIndex(idx); }
    m_rememberCheck->setChecked(cfg.rememberMe);
}

void ConnectModal::setStatusMessage(const QString& msg, bool isError)
{
    if (msg.isEmpty()) {
        m_statusLabel->hide();
        return;
    }
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(
        isError ? QStringLiteral("color:#ff4f4f; font-size:12px;")
        : QStringLiteral("color:#66dd88; font-size:12px;"));
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
        adjustSize();
        emit sizeChanged();
    }
    else {
        m_spinner->stopSpinning();
        // Keep log visible so user can read it after failure
        adjustSize();
        emit sizeChanged();
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ConnectModal::onConnectClicked()
{
    if (!validate()) { return; }

    saveSettings();
    setStatusMessage(QString()); // clear any previous error

    emit connectRequested(currentConfig());
}

void ConnectModal::onCancelClicked()
{
    // Emit cancelled so the overlay is hidden via AppController wiring.
    // Do NOT call reject() — the modal lives inside ModalOverlay, not as a
    // standalone dialog, so reject() would just hide the QDialog and leave
    // the overlay opaque with no content.
    emit cancelled();
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool ConnectModal::validate()
{
    const QString url = m_urlEdit->text().trimmed();
    const QString room = m_roomEdit->text().trimmed();

    if (url.isEmpty()) {
        setStatusMessage(QStringLiteral("Server URL is required."));
        m_urlEdit->setFocus();
        return false;
    }

    // Accept ws://, wss://, http://, https://
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() ||
        (scheme != QLatin1String("ws") &&
            scheme != QLatin1String("wss") &&
            scheme != QLatin1String("http") &&
            scheme != QLatin1String("https")))
    {
        setStatusMessage(
            QStringLiteral("Invalid URL. Use ws://, wss://, http://, or https://."));
        m_urlEdit->setFocus();
        return false;
    }

    if (room.isEmpty()) {
        setStatusMessage(QStringLiteral("Room ID is required."));
        m_roomEdit->setFocus();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ConnectionConfig ConnectModal::currentConfig() const
{
    ConnectionConfig cfg;
    cfg.serverUrl = m_urlEdit->text().trimmed();
    cfg.roomId = m_roomEdit->text().trimmed();
    cfg.password = m_passEdit->text();
    cfg.appType = m_typeCombo->currentData().toString();
    cfg.rememberMe = m_rememberCheck->isChecked();
    return cfg;
}

void ConnectModal::setInputsEnabled(bool enabled)
{
    m_urlEdit->setEnabled(enabled);
    m_roomEdit->setEnabled(enabled);
    m_passEdit->setEnabled(enabled);
    m_typeCombo->setEnabled(enabled);
    m_rememberCheck->setEnabled(enabled);
    m_connectBtn->setEnabled(enabled);
    m_cancelBtn->setEnabled(enabled);
}

// ---------------------------------------------------------------------------
// Rounded-corner painting
// ---------------------------------------------------------------------------

void ConnectModal::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect(), k_radius, k_radius);

    p.fillPath(path, QColor(0x1e, 0x1e, 0x38)); // dark card background
}


void ConnectModal::appendLog(const QString& message)
{
    if (!m_logPanel) { return; }
    m_logPanel->setVisible(true);
    // Timestamp each line
    const QString ts = QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    m_logPanel->append(QStringLiteral("<span style='color:#555588'>%1</span> %2")
        .arg(ts, message.toHtmlEscaped()));
    // Auto-scroll to bottom
    QTextCursor c = m_logPanel->textCursor();
    c.movePosition(QTextCursor::End);
    m_logPanel->setTextCursor(c);
}