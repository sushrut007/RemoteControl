#include "FirstRunInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

namespace {

constexpr auto kInstallFolder = "RemoteControl";
constexpr auto kInstallFlag = "--install";
constexpr auto kShortcutName = "RemoteControl.lnk";

bool hasInstallFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String(kInstallFlag))
            return true;
    }
    return false;
}

QString installDirectory()
{
#ifdef Q_OS_WIN
    const QByteArray programFiles = qgetenv("ProgramFiles");
    const QString root = programFiles.isEmpty()
        ? QStringLiteral("C:/Program Files")
        : QString::fromLocal8Bit(programFiles);
    return QDir(root).filePath(QString::fromLatin1(kInstallFolder));
#else
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
        + QLatin1Char('/') + QLatin1String(kInstallFolder);
#endif
}

QString installedExecutablePath()
{
    const QFileInfo fileInfo(QCoreApplication::applicationFilePath());
    return QDir(installDirectory()).filePath(fileInfo.fileName());
}

bool copyDirectory(const QString& sourceDir, const QString& destinationDir)
{
    const QDir source(sourceDir);
    if (!source.exists())
        return false;

    if (!QDir().mkpath(destinationDir))
        return false;

    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& entry : entries) {
        const QString destinationPath = QDir(destinationDir).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destinationPath))
                return false;
            continue;
        }

        if (QFile::exists(destinationPath) && !QFile::remove(destinationPath))
            return false;

        if (!QFile::copy(entry.absoluteFilePath(), destinationPath))
            return false;
    }

    return true;
}

bool pathsAreEquivalent(const QString& left, const QString& right)
{
    const QString canonicalLeft = QDir(left).canonicalPath();
    const QString canonicalRight = QDir(right).canonicalPath();
    return !canonicalLeft.isEmpty() && canonicalLeft == canonicalRight;
}

#ifdef Q_OS_WIN
bool createDesktopShortcut(const QString& targetExe, const QString& workingDirectory)
{
    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktopPath.isEmpty())
        return false;

    const QString shortcutPath = QDir(desktopPath).filePath(QString::fromLatin1(kShortcutName));

    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comInit))
        return false;

    bool success = false;
    IShellLinkW* shellLink = nullptr;
    IPersistFile* persistFile = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&shellLink)))) {
        shellLink->SetPath(reinterpret_cast<LPCWSTR>(targetExe.utf16()));
        shellLink->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(workingDirectory.utf16()));
        shellLink->SetDescription(L"RemoteControl");

        if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile)))) {
            success = SUCCEEDED(persistFile->Save(reinterpret_cast<LPCWSTR>(shortcutPath.utf16()), TRUE));
            persistFile->Release();
        }

        shellLink->Release();
    }
    
    CoUninitialize();
    return success;
}

bool requestElevatedInstall(const QString& executablePath)
{
    const std::wstring exePath = executablePath.toStdWString();
    const std::wstring parameters = L"--install";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = exePath.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&executeInfo))
        return false;

    if (executeInfo.hProcess)
        CloseHandle(executeInfo.hProcess);

    return true;
}
#endif

void showInstallError(const QString& message)
{
    QMessageBox::critical(
        nullptr,
        QStringLiteral("RemoteControl Setup"),
        message);
}

} // namespace

bool FirstRunInstaller::ensureInstalled(int argc, char* argv[])
{
    const QString currentDirectory = QCoreApplication::applicationDirPath();
    const QString installDir = installDirectory();
    const QString installedExe = installedExecutablePath();
    const bool installMode = hasInstallFlag(argc, argv);

    if (pathsAreEquivalent(currentDirectory, installDir))
        return true;

    if (QFile::exists(installedExe) && !installMode) {
        if (!QProcess::startDetached(installedExe, QStringList())) {
            showInstallError(QStringLiteral("RemoteControl is already installed, but the installed copy could not be started."));
            return false;
        }
        return false;
    }

#ifdef Q_OS_WIN
    if (!installMode) {
        if (!requestElevatedInstall(QCoreApplication::applicationFilePath())) {
            showInstallError(QStringLiteral(
                "Administrator permission is required to install RemoteControl into Program Files.\n"
                "Installation was cancelled or failed."));
            return false;
        }
        return false;
    }

    if (!copyDirectory(currentDirectory, installDir)) {
        showInstallError(QStringLiteral(
            "Failed to copy RemoteControl files to:\n%1").arg(installDir));
        return false;
    }

    if (!createDesktopShortcut(installedExe, installDir)) {
        showInstallError(QStringLiteral(
            "RemoteControl was installed, but the desktop shortcut could not be created."));
    }

    if (!QProcess::startDetached(installedExe, QStringList())) {
        showInstallError(QStringLiteral(
            "RemoteControl was installed to:\n%1\n\nbut the application could not be started.").arg(installDir));
        return false;
    }

    return false;
#else
    Q_UNUSED(installMode);
    return true;
#endif
}
