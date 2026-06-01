#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include "../core/AppState.h"
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QSlider)
QT_FORWARD_DECLARE_CLASS(QRadioButton)
QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)


// ---------------------------------------------------------------------------
// SettingsModal
// ---------------------------------------------------------------------------
class SettingsModal : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsModal(QWidget* parent = nullptr);

    /// Pre-populate the dialog with existing settings.
    void loadSettings(const AppSettings& s);

    /// Return whatever is currently shown in the UI (unsaved).
    AppSettings currentSettings() const;

signals:
    void settingsChanged(const AppSettings& settings);

private slots:
    void onSave();
    void onCancel();
    void onBitrateChanged(int value);
    void onPreviewQualityChanged(int value);
    void onSensitivityChanged(int value);
    void onAddBlockedKey();
    void onRemoveBlockedKey();

private:
    void buildUi();
    QWidget* buildConnectionTab();
    QWidget* buildVideoTab();
    QWidget* buildInputTab();

    void populateMonitors();
    void readFromSettings();
    void writeToSettings(const AppSettings& s);

    // ── Connection tab ────────────────────────────────────────────────────
    QLineEdit* m_serverUrlEdit{ nullptr };
    QLineEdit* m_stunEdit{ nullptr };
    QLineEdit* m_turnEdit{ nullptr };
    QLineEdit* m_turnUserEdit{ nullptr };
    QLineEdit* m_turnPassEdit{ nullptr };
    QSpinBox* m_timeoutSpin{ nullptr };
    QCheckBox* m_autoReconnectCheck{ nullptr };
    QSpinBox* m_maxAttemptsSpin{ nullptr };

    // ── Video tab ─────────────────────────────────────────────────────────
    QComboBox* m_monitorCombo{ nullptr };
    QComboBox* m_fpsCombo{ nullptr };
    QSlider* m_bitrateSlider{ nullptr };
    QLabel* m_bitrateLabel{ nullptr };
    QRadioButton* m_h264Radio{ nullptr };
    QRadioButton* m_vp8Radio{ nullptr };
    QSlider* m_previewQualSlider{ nullptr };
    QLabel* m_previewQualLabel{ nullptr };

    // ── Input tab ─────────────────────────────────────────────────────────
    QCheckBox* m_kbdCheck{ nullptr };
    QCheckBox* m_mouseCheck{ nullptr };
    QSlider* m_sensitivitySlider{ nullptr };
    QLabel* m_sensitivityLabel{ nullptr };
    QListWidget* m_blockedKeysList{ nullptr };
    QLineEdit* m_addKeyEdit{ nullptr };
    QPushButton* m_addKeyBtn{ nullptr };
    QPushButton* m_removeKeyBtn{ nullptr };

    // ── Dialog buttons ────────────────────────────────────────────────────
    QPushButton* m_saveBtn{ nullptr };
    QPushButton* m_cancelBtn{ nullptr };
};