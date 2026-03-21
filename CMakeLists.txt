cmake_minimum_required(VERSION 3.16)
project(ai_frontier VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_BACKEND "Build the native backend API server" ON)
option(BUILD_DASHBOARD_WIDGETS "Build the Qt Widgets dashboard (legacy)" OFF)
option(BUILD_DASHBOARD_QML "Build the Qt Quick/QML dashboard" OFF)

if(BUILD_BACKEND)
    add_subdirectory(backend)
endif()

if(BUILD_DASHBOARD_WIDGETS)
    add_subdirectory(dashboard_widgets)
endif()

if(BUILD_DASHBOARD_QML)
    add_subdirectory(dashboard_qml)
endif()
