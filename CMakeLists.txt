cmake_minimum_required(VERSION 3.16)
project(Project LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Gui
        Widgets
        WebSockets
        Network
        OpenGLWidgets
)
qt_standard_project_setup()

set(PROJECT_SOURCES
    main.cpp
    RemoteDeviceControl.h
    RemoteDeviceControl.cpp
    mediasoup/MediasoupClient.h
    mediasoup/MediasoupClient.cpp
    mediasoup/VideoProducer.h
    mediasoup/VideoProducer.cpp
    mediasoup/VideoDecoder.cpp
    network/SignalingClient.h
    network/SignalingClient.cpp
    core/RoomManager.h
    core/RoomManager.cpp
    core/AppState.h
    core/AppController.h
    core/AppController.cpp
    core/ScreenCapturer.h
    core/ScreenCapturer.cpp
    input/InputInjector.h
    input/InputInjector.cpp
    core/ControlEventHandler.h
    core/ControlEventHandler.cpp
    ui/AppShell.h
    ui/AppShell.cpp
    ui/ConnectModal.h
    ui/ConnectModal.cpp
    ui/ViewerPage.h
    ui/ViewerPage.cpp
    ui/HostPage.h
    ui/HostPage.cpp
    ui/SettingsModal.h
    ui/SettingsModal.cpp
    ui/KickPeerModal.h
    ui/KickPeerModal.cpp
    ui/PeerListWidget.h
    ui/PeerListWidget.cpp
)

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

set(MEDIASOUP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mediasoup-prebuilt")

target_include_directories(${PROJECT_NAME} PRIVATE "${MEDIASOUP_DIR}/include")


target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::WebSockets
        Qt::Network
        Qt::OpenGLWidgets
    PRIVATE
        "${MEDIASOUP_DIR}/lib/mediasoup.lib"
        "${MEDIASOUP_DIR}/lib/uv.lib"
        "${MEDIASOUP_DIR}/lib/netstring.lib"
        "${MEDIASOUP_DIR}/lib/spdlog.lib"
        "${MEDIASOUP_DIR}/lib/srtp2.lib"
        "${MEDIASOUP_DIR}/lib/usrsctp.lib"
        "${MEDIASOUP_DIR}/lib/libwebrtc.lib"
        "${MEDIASOUP_DIR}/lib/libcrypto-1_1-x64.lib"
        "${MEDIASOUP_DIR}/lib/libssl-1_1-x64.lib"
        "${MEDIASOUP_DIR}/lib/wingetopt.lib"
        "${MEDIASOUP_DIR}/lib/absl_bad_optional_access.lib"
        "${MEDIASOUP_DIR}/lib/absl_base.lib"
        "${MEDIASOUP_DIR}/lib/absl_dynamic_annotations.lib"
        "${MEDIASOUP_DIR}/lib/absl_int128.lib"
        "${MEDIASOUP_DIR}/lib/absl_internal_spinlock_wait.lib"
        "${MEDIASOUP_DIR}/lib/absl_internal_strings_internal.lib"
        "${MEDIASOUP_DIR}/lib/absl_internal_throw_delegate.lib"
        "${MEDIASOUP_DIR}/lib/absl_optional.lib"
        "${MEDIASOUP_DIR}/lib/absl_strings.lib"
        d3d11.lib
        dxgi.lib
        gdi32.lib
        mf.lib
        mfplat.lib
        mfuuid.lib
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MEDIASOUP_DIR}/runtime/uv.dll"
        "${MEDIASOUP_DIR}/runtime/usrsctp.dll"
        "${MEDIASOUP_DIR}/runtime/mediasoup-worker.exe"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)