cmake_minimum_required(VERSION 3.16)
project(TiwutLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui WebEngineWidgets Network)

set(SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

add_executable(TiwutLauncher ${SOURCES})

target_link_libraries(TiwutLauncher PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::WebEngineWidgets
    Qt6::Network
)

install(TARGETS TiwutLauncher DESTINATION bin)
install(DIRECTORY src/ DESTINATION bin/src FILES_MATCHING PATTERN "index.html")
