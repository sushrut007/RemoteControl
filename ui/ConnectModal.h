#pragma once

#include <QDialog>
#include <QString>
#include "../core/AppState.h"

QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTextEdit)
QT_FORWARD_DECLARE_CLASS(QGraphicsDropShadowEffect)

// ---------------------------------------------------------------------------
// SpinnerWidget
// ---------------------------------------------------------------------------
class SpinnerWidget : public QWidget
{
    Q_OBJECT
        Q_PROPERTY(int angle READ angle WRITE setAngle)
public:
    explicit SpinnerWidget(QWidget* parent = nullptr);
    int  angle() const { return m_angle; }
    void setAngle(int a);
    void startSpinning();
    void stopSpinning();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    int     m_angle{ 0 };
    QTimer* m_timer{ nullptr };
};

// ---------------------------------------------------------------------------
// ConnectModal
// ---------------------------------------------------------------------------
class ConnectModal : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectModal(QWidget* parent = nullptr);
    ~ConnectModal() override = default;

    void setConfig(const ConnectionConfig& cfg);
    void setStatusMessage(const QString& msg, bool isError = true);
    void setLoading(bool loading);

    /// Append a line to the connection log panel.
    void appendLog(const QString& message);

signals:
    void connectRequested(const ConnectionConfig& config);
    void cancelled();
    void sizeChanged(); // emitted when log panel appears/disappears

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onConnectClicked();
    void onCancelClicked();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    bool validate();
    ConnectionConfig currentConfig() const;
    void setInputsEnabled(bool enabled);

    QLineEdit* m_urlEdit{ nullptr };
    QLineEdit* m_roomEdit{ nullptr };
    QLineEdit* m_passEdit{ nullptr };
    QComboBox* m_typeCombo{ nullptr };
    QCheckBox* m_rememberCheck{ nullptr };
    QLabel* m_statusLabel{ nullptr };
    QPushButton* m_connectBtn{ nullptr };
    QPushButton* m_cancelBtn{ nullptr };
    SpinnerWidget* m_spinner{ nullptr };
    QTextEdit* m_logPanel{ nullptr };  ///< connection debug log
};