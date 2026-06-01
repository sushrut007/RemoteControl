#include "SettingsModal.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QRadioButton>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QSettings>
#include <QFrame>
#include <QScrollArea>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int k_dlgW = 560;
static constexpr int k_dlgH = 420;

static const char* k_org = "Deskshare";
static const char* k_app = "RemoteControl";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QFrame* makeSep(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setObjectName(QStringLiteral("DlgSep"));
    return f;
}

static QFormLayout* makeForm(QWidget* parent = nullptr)
{
    auto* fl = new QFormLayout(parent);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(10);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return fl;
}

// ===========================================================================
// SettingsModal
// ===========================================================================

SettingsModal::SettingsModal(QWidget* parent)
    : QDialog(parent, Qt::Dialog)
{
    setWindowTitle(tr("Settings"));
    setMinimumSize(k_dlgW, k_dlgH);
    resize(k_dlgW, k_dlgH);
    setSizeGripEnabled(true);
    buildUi();
    readFromSettings();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SettingsModal::loadSettings(const AppSettings& s)
{
    // Connection
    m_serverUrlEdit->setText(s.serverUrl);
    m_stunEdit->setText(s.stunServer);
    m_turnEdit->setText(s.turnServer);
    m_turnUserEdit->setText(s.turnUsername);
    m_turnPassEdit->setText(s.turnPassword);
    m_timeoutSpin->setValue(s.connectionTimeoutSec);
    m_autoReconnectCheck->setChecked(s.autoReconnect);
    m_maxAttemptsSpin->setValue(s.maxReconnectAttempts);
    m_maxAttemptsSpin->setEnabled(s.autoReconnect);

    // Video
    {
        const int idx = m_monitorCombo->findData(s.monitorIndex);
        m_monitorCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    {
        const int fpsIdx = m_fpsCombo->findData(s.targetFps);
        m_fpsCombo->setCurrentIndex(fpsIdx >= 0 ? fpsIdx : 1 /*30fps default*/);
    }
    m_bitrateSlider->setValue(s.bitrateKbps);
    m_bitrateLabel->setText(tr("%1 kbps").arg(s.bitrateKbps));
    if (s.codec == QLatin1String("VP8")) { m_vp8Radio->setChecked(true); }
    else { m_h264Radio->setChecked(true); }
    m_previewQualSlider->setValue(s.previewQuality);
    m_previewQualLabel->setText(tr("%1").arg(s.previewQuality));

    // Input
    m_kbdCheck->setChecked(s.keyboardEnabled);
    m_mouseCheck->setChecked(s.mouseEnabled);
    m_sensitivitySlider->setValue(s.mouseSensitivity);
    m_sensitivityLabel->setText(tr("%1").arg(s.mouseSensitivity));
    m_blockedKeysList->clear();
    for (const QString& key : s.blockedKeys) {
        m_blockedKeysList->addItem(key);
    }
}

AppSettings SettingsModal::currentSettings() const
{
    AppSettings s;

    // Connection
    s.serverUrl = m_serverUrlEdit->text().trimmed();
    s.stunServer = m_stunEdit->text().trimmed();
    s.turnServer = m_turnEdit->text().trimmed();
    s.turnUsername = m_turnUserEdit->text().trimmed();
    s.turnPassword = m_turnPassEdit->text();
    s.connectionTimeoutSec = m_timeoutSpin->value();
    s.autoReconnect = m_autoReconnectCheck->isChecked();
    s.maxReconnectAttempts = m_maxAttemptsSpin->value();

    // Video
    s.monitorIndex = m_monitorCombo->currentData().toInt();
    s.targetFps = m_fpsCombo->currentData().toInt();
    s.bitrateKbps = m_bitrateSlider->value();
    s.codec = m_h264Radio->isChecked()
        ? QStringLiteral("H264")
        : QStringLiteral("VP8");
    s.previewQuality = m_previewQualSlider->value();

    // Input
    s.keyboardEnabled = m_kbdCheck->isChecked();
    s.mouseEnabled = m_mouseCheck->isChecked();
    s.mouseSensitivity = m_sensitivitySlider->value();
    for (int i = 0; i < m_blockedKeysList->count(); ++i) {
        s.blockedKeys << m_blockedKeysList->item(i)->text();
    }

    return s;
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void SettingsModal::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("SettingsTabs"));
    tabs->addTab(buildConnectionTab(), tr("Connection"));
    tabs->addTab(buildVideoTab(), tr("Video"));
    tabs->addTab(buildInputTab(), tr("Input Control"));

    rootLayout->addWidget(tabs, 1);
    rootLayout->addWidget(makeSep(this));

    // ── Dialog buttons ────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setObjectName(QStringLiteral("DlgCancelBtn"));
    m_cancelBtn->setFixedHeight(34);

    m_saveBtn = new QPushButton(tr("Save"), this);
    m_saveBtn->setObjectName(QStringLiteral("DlgSaveBtn"));
    m_saveBtn->setFixedHeight(34);
    m_saveBtn->setDefault(true);

    btnRow->addWidget(m_cancelBtn);
    btnRow->addSpacing(8);
    btnRow->addWidget(m_saveBtn);
    rootLayout->addLayout(btnRow);

    QObject::connect(m_saveBtn, &QPushButton::clicked, this, &SettingsModal::onSave);
    QObject::connect(m_cancelBtn, &QPushButton::clicked, this, &SettingsModal::onCancel);

    // ── Stylesheet ────────────────────────────────────────────────────────
    setStyleSheet(QStringLiteral(R"(
        SettingsModal {
            background: #1a1a2e;
        }
        #SettingsTabs {
            background: #1a1a2e;
            border: none;
        }
        QTabBar::tab {
            background: #16213e;
            color: #8888aa;
            border: 1px solid #2a2a5e;
            border-bottom: none;
            border-radius: 5px 5px 0 0;
            padding: 6px 18px;
            font-size: 12px;
        }
        QTabBar::tab:selected {
            background: #0f1630;
            color: #e0e0ff;
            border-bottom: 1px solid #0f1630;
        }
        QTabBar::tab:hover:!selected { background: #1e1e4e; }
        QTabWidget::pane {
            background: #0f1630;
            border: 1px solid #2a2a5e;
            border-radius: 0 5px 5px 5px;
        }
        QGroupBox {
            color: #7070cc;
            font-size: 11px;
            font-weight: bold;
            border: 1px solid #2a2a5e;
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 6px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
        }
        QLabel {
            color: #aaaacc;
            font-size: 12px;
        }
        QLineEdit, QSpinBox, QComboBox {
            background: #16213e;
            color: #e0e0ff;
            border: 1px solid #2a2a5e;
            border-radius: 5px;
            padding: 4px 8px;
            font-size: 12px;
            selection-background-color: #4040a0;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #6c63ff; }
        QSpinBox::up-button, QSpinBox::down-button {
            background: #2a2a5e;
            border: none;
            width: 16px;
        }
        QComboBox::drop-down { border: none; width: 20px; }
        QComboBox QAbstractItemView {
            background: #16213e;
            color: #dcdcf0;
            border: 1px solid #2a2a5e;
            selection-background-color: #2a2a6e;
        }
        QSlider::groove:horizontal {
            background: #2a2a5e;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #6c63ff;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
        }
        QSlider::sub-page:horizontal { background: #6c63ff; border-radius: 2px; }
        QCheckBox, QRadioButton {
            color: #ccccee;
            font-size: 12px;
            spacing: 7px;
        }
        QCheckBox::indicator, QRadioButton::indicator {
            width: 15px; height: 15px;
        }
        QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {
            border: 1px solid #4a4a8e;
            border-radius: 3px;
            background: #16213e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #6c63ff;
            border-radius: 3px;
            background: #6c63ff;
        }
        QRadioButton::indicator { border-radius: 8px; }
        QRadioButton::indicator:checked {
            border: 1px solid #6c63ff;
            background: #6c63ff;
        }
        QListWidget {
            background: #16213e;
            color: #ccccee;
            border: 1px solid #2a2a5e;
            border-radius: 5px;
            font-size: 12px;
        }
        QListWidget::item:selected { background: #2a2a6e; color: #fff; }
        #DlgSep { color: #2a2a5e; }
        #DlgSaveBtn {
            background: #6c63ff;
            color: #ffffff;
            border: none;
            border-radius: 7px;
            padding: 0 24px;
            font-size: 13px;
            font-weight: bold;
        }
        #DlgSaveBtn:hover   { background: #7d75ff; }
        #DlgSaveBtn:pressed { background: #5548e0; }
        #DlgCancelBtn {
            background: #1a1a3e;
            color: #aaaacc;
            border: 1px solid #2a2a5e;
            border-radius: 7px;
            padding: 0 20px;
            font-size: 13px;
        }
        #DlgCancelBtn:hover  { background: #2a2a5e; color: #ffffff; }
        QScrollArea { background: transparent; border: none; }
        QScrollArea > QWidget > QWidget { background: transparent; }
        QScrollBar:vertical {
            background: #16213e; width: 8px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3a7e; border-radius: 4px; min-height: 20px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )"));
}

QWidget* SettingsModal::buildConnectionTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    // ── Server ────────────────────────────────────────────────────────────
    auto* serverGroup = new QGroupBox(tr("Server"), page);
    auto* serverForm = makeForm(serverGroup);
    auto* sgLayout = new QVBoxLayout(serverGroup);
    sgLayout->addLayout(serverForm);

    m_serverUrlEdit = new QLineEdit(serverGroup);
    m_serverUrlEdit->setPlaceholderText(QStringLiteral("ws://host:port"));
    serverForm->addRow(tr("Server URL"), m_serverUrlEdit);

    layout->addWidget(serverGroup);

    // ── ICE / TURN ─────────────────────────────────────────────────────────
    auto* iceGroup = new QGroupBox(tr("ICE Servers"), page);
    auto* iceForm = makeForm(iceGroup);
    auto* igLayout = new QVBoxLayout(iceGroup);
    igLayout->addLayout(iceForm);

    m_stunEdit = new QLineEdit(iceGroup);
    m_stunEdit->setPlaceholderText(QStringLiteral("stun:host:port"));
    iceForm->addRow(tr("STUN Server"), m_stunEdit);

    m_turnEdit = new QLineEdit(iceGroup);
    m_turnEdit->setPlaceholderText(QStringLiteral("turn:host:port  (optional)"));
    iceForm->addRow(tr("TURN Server"), m_turnEdit);

    m_turnUserEdit = new QLineEdit(iceGroup);
    m_turnUserEdit->setPlaceholderText(tr("username"));
    iceForm->addRow(tr("TURN Username"), m_turnUserEdit);

    m_turnPassEdit = new QLineEdit(iceGroup);
    m_turnPassEdit->setEchoMode(QLineEdit::Password);
    m_turnPassEdit->setPlaceholderText(tr("password"));
    iceForm->addRow(tr("TURN Password"), m_turnPassEdit);

    layout->addWidget(iceGroup);

    // ── Reconnection ──────────────────────────────────────────────────────
    auto* reconGroup = new QGroupBox(tr("Reconnection"), page);
    auto* reconForm = makeForm(reconGroup);
    auto* rgLayout = new QVBoxLayout(reconGroup);
    rgLayout->addLayout(reconForm);

    m_timeoutSpin = new QSpinBox(reconGroup);
    m_timeoutSpin->setRange(5, 60);
    m_timeoutSpin->setSuffix(tr(" s"));
    reconForm->addRow(tr("Timeout"), m_timeoutSpin);

    m_autoReconnectCheck = new QCheckBox(tr("Auto-reconnect"), reconGroup);
    reconForm->addRow(QString(), m_autoReconnectCheck);

    m_maxAttemptsSpin = new QSpinBox(reconGroup);
    m_maxAttemptsSpin->setRange(1, 20);
    reconForm->addRow(tr("Max attempts"), m_maxAttemptsSpin);

    QObject::connect(m_autoReconnectCheck, &QCheckBox::toggled,
        m_maxAttemptsSpin, &QSpinBox::setEnabled);

    layout->addWidget(reconGroup);
    layout->addStretch();

    // Wrap in a scroll area so the content is never squeezed.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget* SettingsModal::buildVideoTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* form = makeForm();

    // Monitor
    m_monitorCombo = new QComboBox(page);
    populateMonitors();
    form->addRow(tr("Capture Monitor"), m_monitorCombo);

    // FPS
    m_fpsCombo = new QComboBox(page);
    m_fpsCombo->addItem(QStringLiteral("15 fps"), 15);
    m_fpsCombo->addItem(QStringLiteral("30 fps"), 30);
    m_fpsCombo->addItem(QStringLiteral("60 fps"), 60);
    m_fpsCombo->setCurrentIndex(1);
    form->addRow(tr("Target FPS"), m_fpsCombo);

    // Bitrate
    {
        auto* bitrateRow = new QHBoxLayout();
        m_bitrateSlider = new QSlider(Qt::Horizontal, page);
        m_bitrateSlider->setRange(500, 8000);
        m_bitrateSlider->setSingleStep(100);
        m_bitrateSlider->setPageStep(500);
        m_bitrateLabel = new QLabel(QStringLiteral("2000 kbps"), page);
        m_bitrateLabel->setFixedWidth(80);
        bitrateRow->addWidget(m_bitrateSlider, 1);
        bitrateRow->addWidget(m_bitrateLabel);
        form->addRow(tr("Bitrate"), bitrateRow);
        QObject::connect(m_bitrateSlider, &QSlider::valueChanged,
            this, &SettingsModal::onBitrateChanged);
    }

    // Codec
    {
        auto* codecBox = new QHBoxLayout();
        auto* grp = new QButtonGroup(page);
        m_h264Radio = new QRadioButton(QStringLiteral("H.264"), page);
        m_vp8Radio = new QRadioButton(QStringLiteral("VP8"), page);
        grp->addButton(m_h264Radio);
        grp->addButton(m_vp8Radio);
        m_h264Radio->setChecked(true);
        codecBox->addWidget(m_h264Radio);
        codecBox->addSpacing(16);
        codecBox->addWidget(m_vp8Radio);
        codecBox->addStretch();
        form->addRow(tr("Codec"), codecBox);
    }

    // Preview quality
    {
        auto* qualRow = new QHBoxLayout();
        m_previewQualSlider = new QSlider(Qt::Horizontal, page);
        m_previewQualSlider->setRange(10, 100);
        m_previewQualSlider->setSingleStep(5);
        m_previewQualLabel = new QLabel(QStringLiteral("75"), page);
        m_previewQualLabel->setFixedWidth(30);
        qualRow->addWidget(m_previewQualSlider, 1);
        qualRow->addWidget(m_previewQualLabel);
        form->addRow(tr("Preview Quality"), qualRow);
        QObject::connect(m_previewQualSlider, &QSlider::valueChanged,
            this, &SettingsModal::onPreviewQualityChanged);
    }

    layout->addLayout(form);
    layout->addStretch();

    // Wrap in a scroll area so the content is never squeezed.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget* SettingsModal::buildInputTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    // ── Toggles ───────────────────────────────────────────────────────────
    auto* toggleGroup = new QGroupBox(tr("Allowed Input"), page);
    auto* toggleLayout = new QVBoxLayout(toggleGroup);
    m_kbdCheck = new QCheckBox(tr("Enable keyboard control"), toggleGroup);
    m_mouseCheck = new QCheckBox(tr("Enable mouse control"), toggleGroup);
    m_kbdCheck->setChecked(true);
    m_mouseCheck->setChecked(true);
    toggleLayout->addWidget(m_kbdCheck);
    toggleLayout->addWidget(m_mouseCheck);
    layout->addWidget(toggleGroup);

    // ── Mouse sensitivity ─────────────────────────────────────────────────
    auto* sensGroup = new QGroupBox(tr("Mouse Sensitivity"), page);
    auto* sensLayout = new QHBoxLayout(sensGroup);
    m_sensitivitySlider = new QSlider(Qt::Horizontal, sensGroup);
    m_sensitivitySlider->setRange(1, 100);
    m_sensitivitySlider->setSingleStep(5);
    m_sensitivityLabel = new QLabel(QStringLiteral("50"), sensGroup);
    m_sensitivityLabel->setFixedWidth(30);
    sensLayout->addWidget(new QLabel(tr("Low"), sensGroup));
    sensLayout->addWidget(m_sensitivitySlider, 1);
    sensLayout->addWidget(new QLabel(tr("High"), sensGroup));
    sensLayout->addSpacing(8);
    sensLayout->addWidget(m_sensitivityLabel);
    QObject::connect(m_sensitivitySlider, &QSlider::valueChanged,
        this, &SettingsModal::onSensitivityChanged);
    layout->addWidget(sensGroup);

    // ── Blocked keys ─────────────────────────────────────────────────────
    auto* keysGroup = new QGroupBox(tr("Blocked Keys"), page);
    auto* keysLayout = new QVBoxLayout(keysGroup);

    m_blockedKeysList = new QListWidget(keysGroup);
    m_blockedKeysList->setFixedHeight(90);
    keysLayout->addWidget(m_blockedKeysList);

    auto* addRow = new QHBoxLayout();
    m_addKeyEdit = new QLineEdit(keysGroup);
    m_addKeyEdit->setPlaceholderText(tr("Key name, e.g. F4 or ctrl+alt+del"));
    m_addKeyEdit->setMaxLength(32);
    m_addKeyBtn = new QPushButton(tr("Add"), keysGroup);
    m_addKeyBtn->setObjectName(QStringLiteral("DlgCancelBtn")); // reuse subtle style
    m_removeKeyBtn = new QPushButton(tr("Remove"), keysGroup);
    m_removeKeyBtn->setObjectName(QStringLiteral("DlgCancelBtn"));
    addRow->addWidget(m_addKeyEdit, 1);
    addRow->addWidget(m_addKeyBtn);
    addRow->addWidget(m_removeKeyBtn);
    keysLayout->addLayout(addRow);

    QObject::connect(m_addKeyBtn, &QPushButton::clicked, this, &SettingsModal::onAddBlockedKey);
    QObject::connect(m_removeKeyBtn, &QPushButton::clicked, this, &SettingsModal::onRemoveBlockedKey);
    QObject::connect(m_addKeyEdit, &QLineEdit::returnPressed, this, &SettingsModal::onAddBlockedKey);

    layout->addWidget(keysGroup);
    layout->addStretch();

    // Wrap in a scroll area so the content is never squeezed.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Monitor enumeration (Windows only)
// ---------------------------------------------------------------------------

void SettingsModal::populateMonitors()
{
    m_monitorCombo->clear();

#ifdef Q_OS_WIN
    int index = 0;
    DISPLAY_DEVICEW dd;
    dd.cb = sizeof(dd);
    while (EnumDisplayDevicesW(nullptr, index, &dd, 0)) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            const QString name = QString::fromWCharArray(dd.DeviceName);
            const QString friendly =
                (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
                ? tr("Monitor %1: %2 (Primary)").arg(index + 1).arg(name)
                : tr("Monitor %1: %2").arg(index + 1).arg(name);
            m_monitorCombo->addItem(friendly, index);
        }
        ++index;
        dd.cb = sizeof(dd);
    }
#endif

    if (m_monitorCombo->count() == 0) {
        m_monitorCombo->addItem(tr("Primary Monitor"), 0);
    }
}

// ---------------------------------------------------------------------------
// QSettings persistence
// ---------------------------------------------------------------------------

void SettingsModal::readFromSettings()
{
    QSettings s{ k_org, k_app };

    AppSettings def;
    AppSettings loaded;

    s.beginGroup(QStringLiteral("Connection"));
    loaded.serverUrl = s.value(QStringLiteral("serverUrl"), def.serverUrl).toString();
    loaded.stunServer = s.value(QStringLiteral("stunServer"), def.stunServer).toString();
    loaded.turnServer = s.value(QStringLiteral("turnServer"), def.turnServer).toString();
    loaded.turnUsername = s.value(QStringLiteral("turnUsername"), def.turnUsername).toString();
    loaded.turnPassword = s.value(QStringLiteral("turnPassword"), def.turnPassword).toString();
    loaded.connectionTimeoutSec = s.value(QStringLiteral("connectionTimeoutSec"), def.connectionTimeoutSec).toInt();
    loaded.autoReconnect = s.value(QStringLiteral("autoReconnect"), def.autoReconnect).toBool();
    loaded.maxReconnectAttempts = s.value(QStringLiteral("maxReconnectAttempts"), def.maxReconnectAttempts).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("Video"));
    loaded.monitorIndex = s.value(QStringLiteral("monitorIndex"), def.monitorIndex).toInt();
    loaded.targetFps = s.value(QStringLiteral("targetFps"), def.targetFps).toInt();
    loaded.bitrateKbps = s.value(QStringLiteral("bitrateKbps"), def.bitrateKbps).toInt();
    loaded.codec = s.value(QStringLiteral("codec"), def.codec).toString();
    loaded.previewQuality = s.value(QStringLiteral("previewQuality"), def.previewQuality).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("Input"));
    loaded.keyboardEnabled = s.value(QStringLiteral("keyboardEnabled"), def.keyboardEnabled).toBool();
    loaded.mouseEnabled = s.value(QStringLiteral("mouseEnabled"), def.mouseEnabled).toBool();
    loaded.mouseSensitivity = s.value(QStringLiteral("mouseSensitivity"), def.mouseSensitivity).toInt();
    loaded.blockedKeys = s.value(QStringLiteral("blockedKeys"), def.blockedKeys).toStringList();
    s.endGroup();

    loadSettings(loaded);
}

void SettingsModal::writeToSettings(const AppSettings& s)
{
    QSettings st{ k_org, k_app };

    st.beginGroup(QStringLiteral("Connection"));
    st.setValue(QStringLiteral("serverUrl"), s.serverUrl);
    st.setValue(QStringLiteral("stunServer"), s.stunServer);
    st.setValue(QStringLiteral("turnServer"), s.turnServer);
    st.setValue(QStringLiteral("turnUsername"), s.turnUsername);
    st.setValue(QStringLiteral("turnPassword"), s.turnPassword);
    st.setValue(QStringLiteral("connectionTimeoutSec"), s.connectionTimeoutSec);
    st.setValue(QStringLiteral("autoReconnect"), s.autoReconnect);
    st.setValue(QStringLiteral("maxReconnectAttempts"), s.maxReconnectAttempts);
    st.endGroup();

    st.beginGroup(QStringLiteral("Video"));
    st.setValue(QStringLiteral("monitorIndex"), s.monitorIndex);
    st.setValue(QStringLiteral("targetFps"), s.targetFps);
    st.setValue(QStringLiteral("bitrateKbps"), s.bitrateKbps);
    st.setValue(QStringLiteral("codec"), s.codec);
    st.setValue(QStringLiteral("previewQuality"), s.previewQuality);
    st.endGroup();

    st.beginGroup(QStringLiteral("Input"));
    st.setValue(QStringLiteral("keyboardEnabled"), s.keyboardEnabled);
    st.setValue(QStringLiteral("mouseEnabled"), s.mouseEnabled);
    st.setValue(QStringLiteral("mouseSensitivity"), s.mouseSensitivity);
    st.setValue(QStringLiteral("blockedKeys"), s.blockedKeys);
    st.endGroup();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SettingsModal::onSave()
{
    const AppSettings s = currentSettings();
    writeToSettings(s);
    emit settingsChanged(s);
    accept();
}

void SettingsModal::onCancel()
{
    reject();
}

void SettingsModal::onBitrateChanged(int value)
{
    m_bitrateLabel->setText(tr("%1 kbps").arg(value));
}

void SettingsModal::onPreviewQualityChanged(int value)
{
    m_previewQualLabel->setText(tr("%1").arg(value));
}

void SettingsModal::onSensitivityChanged(int value)
{
    m_sensitivityLabel->setText(tr("%1").arg(value));
}

void SettingsModal::onAddBlockedKey()
{
    const QString key = m_addKeyEdit->text().trimmed();
    if (key.isEmpty()) { return; }

    // Avoid duplicates (case-insensitive)
    for (int i = 0; i < m_blockedKeysList->count(); ++i) {
        if (m_blockedKeysList->item(i)->text().compare(key, Qt::CaseInsensitive) == 0) {
            m_addKeyEdit->clear();
            return;
        }
    }
    m_blockedKeysList->addItem(key);
    m_addKeyEdit->clear();
}

void SettingsModal::onRemoveBlockedKey()
{
    const auto selected = m_blockedKeysList->selectedItems();
    for (auto* item : selected) {
        delete m_blockedKeysList->takeItem(m_blockedKeysList->row(item));
    }
}