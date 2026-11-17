cmake_minimum_required(VERSION 3.20)
project(EchoPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Network DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat IMPORTED_TARGET)
pkg_check_modules(AVCODEC REQUIRED libavcodec IMPORTED_TARGET)
pkg_check_modules(AVUTIL REQUIRED libavutil IMPORTED_TARGET)
pkg_check_modules(SWSCALE REQUIRED libswscale IMPORTED_TARGET)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample IMPORTED_TARGET)
pkg_check_modules(MPV REQUIRED mpv IMPORTED_TARGET)

set(SOURCES
    src/main.cpp
    src/i18n/languagemanager.cpp
    src/ui/mainwindow.cpp
    src/ui/titlebar.cpp
    src/ui/controlbar.cpp
    src/ui/spectrumwidget.cpp
    src/ui/mpvwidget.cpp
    src/ui/thumbnailprovider.cpp
    src/ui/adjustpanel.cpp
    src/core/audioengine.cpp
    src/core/mpriscontroller.cpp
    src/core/screensaverinhibitor.cpp
    src/core/statusnotifieritem.cpp
    resources/resources.qrc
)

set(HEADERS
    src/i18n/languagemanager.h
    src/ui/mainwindow.h
    src/ui/titlebar.h
    src/ui/controlbar.h
    src/ui/spectrumwidget.h
    src/ui/mpvwidget.h
    src/ui/thumbnailprovider.h
    src/ui/adjustpanel.h
    src/core/audioengine.h
    src/core/mpriscontroller.h
    src/core/screensaverinhibitor.h
    src/core/statusnotifieritem.h
    src/core/types.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network Qt6::DBus
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::MPV
)

# --- Playlist App (standalone) ---
set(PLAYLIST_SOURCES
    src/playlist/main.cpp
    src/playlist/playlistapp.cpp
    src/i18n/languagemanager.cpp
)

set(PLAYLIST_HEADERS
    src/playlist/playlistapp.h
    src/playlist/playlistprotocol.h
    src/i18n/languagemanager.h
)

add_executable(${PROJECT_NAME}-playlist ${PLAYLIST_SOURCES} ${PLAYLIST_HEADERS})

target_link_libraries(${PROJECT_NAME}-playlist PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
)

target_include_directories(${PROJECT_NAME}-playlist PRIVATE
    src
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
    ${MPV_INCLUDE_DIRS}
    src
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVCODEC_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
    ${SWRESAMPLE_LIBRARY_DIRS}
    ${MPV_LIBRARY_DIRS}
)

# --- Uninstall (for cmake --install based installs) ---
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/cmake_uninstall.cmake.in"
        "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    )
endif()

# --- Install ---
install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}-playlist
    RUNTIME DESTINATION bin
)

install(FILES resources/icon.png
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME org.echoplayer.EchoPlayer.png
)

install(FILES packaging/echo-player.desktop
    DESTINATION share/applications
    RENAME org.echoplayer.EchoPlayer.desktop
)

install(FILES packaging/echo-player.metainfo.xml
    DESTINATION share/metainfo
    RENAME org.echoplayer.EchoPlayer.metainfo.xml
)

install(FILES
    src/i18n/echoPlayer_en.ts
    src/i18n/echoPlayer_zh_CN.ts
    src/i18n/echoPlayer_zh_TW.ts
    DESTINATION share/EchoPlayer/translations
)

# --- CPack ---
set(CPACK_PACKAGE_NAME "echo-player")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A media player built with Qt6 + mpv render API + FFmpeg")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/anomalyco/EchoPlayer")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "EchoPlayer Team")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6 (>= 6.5), libqt6gui6 (>= 6.5), libqt6widgets6 (>= 6.5), libqt6opengl6 (>= 6.5), libqt6openglwidgets6 (>= 6.5), libqt6network6 (>= 6.5), libqt6dbus6 (>= 6.5), libavformat60 (>= 6.0), libavcodec60 (>= 6.0), libavutil58 (>= 6.0), libswresample4 (>= 6.0), libmpv2 (>= 0.34)"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}_amd64")

include(CPack)
