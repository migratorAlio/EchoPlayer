cmake_minimum_required(VERSION 3.20)
project(EchoPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat IMPORTED_TARGET)
pkg_check_modules(AVCODEC REQUIRED libavcodec IMPORTED_TARGET)
pkg_check_modules(AVUTIL REQUIRED libavutil IMPORTED_TARGET)
pkg_check_modules(SWSCALE REQUIRED libswscale IMPORTED_TARGET)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample IMPORTED_TARGET)
pkg_check_modules(SDL2 REQUIRED sdl2 IMPORTED_TARGET)
pkg_check_modules(MPV REQUIRED mpv IMPORTED_TARGET)

set(SOURCES
    src/main.cpp
    src/i18n/languagemanager.cpp
    src/ui/mainwindow.cpp
    src/ui/titlebar.cpp
    src/ui/controlbar.cpp
    src/ui/spectrumwidget.cpp
    src/ui/mpvwidget.cpp
    src/core/audioengine.cpp
    resources/resources.qrc
)

set(HEADERS
    src/i18n/languagemanager.h
    src/ui/mainwindow.h
    src/ui/titlebar.h
    src/ui/controlbar.h
    src/ui/spectrumwidget.h
    src/ui/mpvwidget.h
    src/core/audioengine.h
    src/core/types.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
    xcb
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::SDL2
    PkgConfig::MPV
)

# --- Playlist App (standalone) ---
set(PLAYLIST_SOURCES
    src/playlist/main.cpp
    src/playlist/playlistapp.cpp
)

set(PLAYLIST_HEADERS
    src/playlist/playlistapp.h
    src/playlist/playlistprotocol.h
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
    ${SDL2_INCLUDE_DIRS}
    ${MPV_INCLUDE_DIRS}
    src
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVCODEC_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
    ${SWRESAMPLE_LIBRARY_DIRS}
    ${SDL2_LIBRARY_DIRS}
    ${MPV_LIBRARY_DIRS}
)
