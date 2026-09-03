cmake_minimum_required(VERSION 3.16)
project(Darpan LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Gui
        Widgets
        Svg
        WebSockets
        Network
        OpenGLWidgets
)
qt_standard_project_setup()

set(PROJECT_SOURCES
    main.cpp
    Darpan.qrc
    Darpan.rc
    FirstRunInstaller.h
    FirstRunInstaller.cpp
    RemoteDeviceControl.h
    RemoteDeviceControl.cpp
    network/P2PClient.h
    network/P2PClient.cpp
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
    ui/DarpanIcons.h
    ui/DarpanIcons.cpp
    ui/DarpanTheme.h
    ui/DarpanStyle.h
    ui/DarpanStyle.cpp
    ui/RoleSelectorWidget.h
    ui/RoleSelectorWidget.cpp
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

set(DATACHANNEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libdatachannel-prebuilt")

target_include_directories(${PROJECT_NAME} PRIVATE 
    "${DATACHANNEL_DIR}/include"
    "${DATACHANNEL_DIR}/include/rtc"
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::WebSockets
        Qt::Network
        Qt::OpenGLWidgets
    PRIVATE
        $<$<CONFIG:Debug>:${DATACHANNEL_DIR}/lib/Debug/datachannel.lib>
        $<$<NOT:$<CONFIG:Debug>>:${DATACHANNEL_DIR}/lib/Release/datachannel.lib>
        d3d11.lib
        dxgi.lib
        gdi32.lib
        mf.lib
        mfplat.lib
        mfuuid.lib
        ws2_32.lib
        crypt32.lib
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:"${DATACHANNEL_DIR}/bin/Debug/datachannel.dll">
        $<$<NOT:$<CONFIG:Debug>>:"${DATACHANNEL_DIR}/bin/Release/datachannel.dll">
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/styles/darpan.qss"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles/darpan.qss"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/icons"
)
