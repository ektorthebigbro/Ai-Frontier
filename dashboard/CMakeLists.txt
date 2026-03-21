cmake_minimum_required(VERSION 3.16)
project(ai_frontier_backend VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(ai_frontier_backend_api
    backend_main.cpp
    backend.h
    http/simple_http_server.h
    http/simple_http_server.cpp
    config/simple_yaml.cpp
    runtime/backend_core.cpp
    runtime/backend_console.cpp
    runtime/backend_state.cpp
    runtime/backend_process.cpp
    runtime/backend_diagnostics.cpp
    runtime/backend_generate.cpp
    runtime/backend_server.cpp
    runtime/backend_server_metrics.cpp
)

target_compile_definitions(ai_frontier_backend_api PRIVATE
    AI_FRONTIER_VERSION="${PROJECT_VERSION}"
)

target_include_directories(ai_frontier_backend_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    common
    config
    http
    runtime
)

target_link_libraries(ai_frontier_backend_api PRIVATE
    Qt6::Core
    Qt6::Network
)
