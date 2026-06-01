#pragma once

#include <QObject>

// Forward declarations – full headers included in .cpp only
class AppShell;
class AppController;
class ConnectModal;
class ViewerPage;
class HostPage;

/**
 * RemoteDeviceControl
 *
 * Application bootstrapper.  Creates and wires together:
 *   AppShell  (main window, frameless dark UI)
 *   ConnectModal, ViewerPage, HostPage  (page widgets injected into AppShell)
 *   AppController  (connects all business-logic components)
 *
 * Usage:
 *   RemoteDeviceControl app;
 *   app.launch();   // shows the window and opens the connect dialog
 */
class RemoteDeviceControl : public QObject
{
    Q_OBJECT

public:
    explicit RemoteDeviceControl(QObject* parent = nullptr);
    ~RemoteDeviceControl() override;

    /// Show the main window and present the connect dialog.
    void launch();

    /// Accessor for the main window (e.g. for centering on screen).
    AppShell* shell() const { return m_shell; }

private:
    AppShell* m_shell{ nullptr };
    ConnectModal* m_connectModal{ nullptr };
    ViewerPage* m_viewerPage{ nullptr };
    HostPage* m_hostPage{ nullptr };
    AppController* m_controller{ nullptr };
};