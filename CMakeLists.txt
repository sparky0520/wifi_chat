cmake_minimum_required(VERSION 3.16)

project(LANChat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
)

set(HEADERS
    src/MainWindow.h
)

qt_add_executable(LANChat
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(LANChat PRIVATE Qt6::Widgets Qt6::Network)
