#pragma once

#include <QDialog>
#include <QString>
#include "HostPage.h" // PeerInfo

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QPushButton)

class KickPeerModal : public QDialog
{
    Q_OBJECT
public:
    explicit KickPeerModal(const PeerInfo& peer, QWidget* parent = nullptr);

signals:
    void kickConfirmed(const QString& peerId, const QString& reason);

private slots:
    void onKickClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void buildUi(const PeerInfo& peer);

    QString      m_peerId;
    QLineEdit* m_reasonEdit{ nullptr };
    QPushButton* m_kickBtn{ nullptr };
    QPushButton* m_cancelBtn{ nullptr };
};