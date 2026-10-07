cmake_minimum_required(VERSION 3.21)
project(Darpan LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckQtVersion.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibDataChannel.cmake)

find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core Widgets WebSockets Network)

darpan_require_qt_version()

set(DARPAN_SOURCES
    main.cpp
    darpan.cpp
    darpan.h
    Signaling/QtSignalingClient.cpp
    Signaling/QtSignalingClient.h
    Signaling/SignalingController.cpp
    Signaling/SignalingController.h
    Signaling/SignalingClient.h
    RemoteSession/WebRtcSession.cpp
    RemoteSession/WebRtcSession.h
    RemoteSession/SessionCoordinator.cpp
    RemoteSession/SessionCoordinator.h
    RemoteSession/ControlProtocol.cpp
    RemoteSession/ControlProtocol.h
    RemoteSession/FileTransferManager.cpp
    RemoteSession/FileTransferManager.h
    Platform/Windows/ScreenCapturer.cpp
    Platform/Windows/ScreenCapturer.h
    Platform/Windows/ScreenCapturerDxgi.cpp
    Platform/Windows/ScreenCapturerDxgi.h
    Platform/Windows/MfH264Codec.cpp
    Platform/Windows/MfH264Codec.h
    Platform/Windows/InputInjector.cpp
    Platform/Windows/InputInjector.h
    Ui/VideoDisplayWidget.cpp
    Ui/VideoDisplayWidget.h
    Ui/FileTransferPanel.cpp
    Ui/FileTransferPanel.h
    Utils/AppSettings.cpp
    Utils/AppSettings.h
    Utils/WebSocketUrl.cpp
    Utils/WebSocketUrl.h
)

qt_add_executable(Darpan ${DARPAN_SOURCES})

qt_add_resources(Darpan darpan_styles
    PREFIX /
    FILES
        Ui/styles/darpan_dark.qss
)

target_include_directories(Darpan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(Darpan PRIVATE RTC_ENABLE_MEDIA=1)

target_link_libraries(Darpan PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Network
)

darpan_link_libdatachannel(Darpan)

if(WIN32)
    set_target_properties(Darpan PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(Darpan PRIVATE d3d11 dxgi)
    find_program(DARPAN_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(DARPAN_WINDEPLOYQT)
        add_custom_command(
            TARGET Darpan
            POST_BUILD
            COMMAND "${DARPAN_WINDEPLOYQT}" --no-translations "$<TARGET_FILE:Darpan>"
            COMMENT "Deploy Qt runtime and plugins next to Darpan.exe")
    endif()
endif()

install(TARGETS Darpan RUNTIME DESTINATION bin)
