#pragma once

#include "RemoteSession/SessionCoordinator.h"
#include "Signaling/SignalingController.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QFrame;
class QVBoxLayout;
class QTabWidget;
class QSplitter;
class FileTransferPanel;
class VideoDisplayWidget;

class DarpanMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DarpanMainWindow(QWidget *parent = nullptr);

    void setStatusText(const QString &text);
    void showRoomPage(const QString &roomId, bool isHost);
    void showHomePages();
    void setRoomMembers(const QVector<RoomMemberInfo> &members, const QString &selfDeviceId);
    void setRemoteVideoFrame(const QImage &frame);
    void setPeerReconnectVisible(bool visible);
    void updateControlButtons(bool isHost, bool selfControlGranted, bool peerHasControl, bool requestPending);
    void updateHostScreenShareButton(bool sharingActive);
    void setRoomHostUi(bool isHost);
    void setCaptureBackendText(const QString &backend);
    void setSessionSharingIndicator(bool active, const QString &message);

    FileTransferPanel *fileTransferPanel() const { return m_fileTransferPanel; }
    void recordRecentRoom(const QString &roomId, bool pinRequired);
    void refreshRecentRoomCards();

    VideoDisplayWidget *videoWidget() const { return m_videoWidget; }
    bool confirmGrantControl(const QString &peerDisplayName);

    darpan::SessionCoordinator *coordinator() const { return m_coordinator; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void loadStyleSheet();
    QWidget *buildNavRail(QButtonGroup *group, QStackedWidget *stack);
    QWidget *buildPageHost();
    QWidget *buildPageJoin();
    QWidget *buildPageSettings();
    QWidget *buildPageRoom();
    void joinRecentRoom(const QString &roomId, bool pinRequired);

    QStackedWidget *m_stack = nullptr;
    QWidget *m_contentColumn = nullptr;
    QLabel *m_statusLabel = nullptr;

    QLineEdit *m_hostPinEdit = nullptr;
    QPushButton *m_hostCreateBtn = nullptr;
    QLineEdit *m_joinCodeEdit = nullptr;
    QLineEdit *m_joinPinEdit = nullptr;
    QPushButton *m_joinBtn = nullptr;

    QLineEdit *m_settingsNameEdit = nullptr;
    QLineEdit *m_settingsSignalEdit = nullptr;
    QLineEdit *m_settingsStunEdit = nullptr;
    QLineEdit *m_settingsTurnEdit = nullptr;
    QLineEdit *m_settingsTurnUserEdit = nullptr;
    QLineEdit *m_settingsTurnPassEdit = nullptr;
    QCheckBox *m_settingsPinEnable = nullptr;
    QLineEdit *m_settingsPinShaEdit = nullptr;

    QLabel *m_privacyBanner = nullptr;
    QLabel *m_roomCodeLabel = nullptr;
    QPushButton *m_copyRoomCodeBtn = nullptr;
    QLabel *m_roomConnectionLabel = nullptr;
    QListWidget *m_memberList = nullptr;
    VideoDisplayWidget *m_videoWidget = nullptr;
    QPushButton *m_leaveRoomBtn = nullptr;
    QPushButton *m_reconnectSignalBtn = nullptr;
    QPushButton *m_reconnectPeerBtn = nullptr;
    QPushButton *m_requestControlBtn = nullptr;
    QPushButton *m_revokeControlBtn = nullptr;
    QPushButton *m_sendFileBtn = nullptr;
    QPushButton *m_toggleScreenShareBtn = nullptr;
    QLabel *m_captureBackendLabel = nullptr;
    QPushButton *m_fullscreenVideoBtn = nullptr;
    QWidget *m_videoPanel = nullptr;
    FileTransferPanel *m_fileTransferPanel = nullptr;
    QTabWidget *m_roomSessionTabs = nullptr;
    QWidget *m_roomSidePanel = nullptr;
    QSplitter *m_roomSplitter = nullptr;

    static constexpr int kRoomSidePanelMinWidth = 260;
    static constexpr int kRoomSidePanelMaxWidth = 440;

    QVBoxLayout *m_recentRoomsLayout = nullptr;

    QFrame *m_navRail = nullptr;
    QButtonGroup *m_navGroup = nullptr;

    int m_pageRoomIndex = 3;
    darpan::SessionCoordinator *m_coordinator = nullptr;
};
