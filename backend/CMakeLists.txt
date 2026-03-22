cmake_minimum_required(VERSION 3.21)
project(ai_frontier_backend VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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

qt_add_executable(ai_frontier_native_worker
    native/native_worker_main.cpp
    native/native_worker_runtime.cpp
    config/simple_yaml.cpp
)

add_library(ai_frontier_runtime_module MODULE
    native/default_worker_module.cpp
    native/native_worker_runtime.cpp
    config/simple_yaml.cpp
    http/simple_http_server.cpp
)

target_compile_definitions(ai_frontier_backend_api PRIVATE
    AI_FRONTIER_VERSION="${PROJECT_VERSION}"
)

target_compile_definitions(ai_frontier_native_worker PRIVATE
    AI_FRONTIER_VERSION="${PROJECT_VERSION}"
)

target_compile_definitions(ai_frontier_runtime_module PRIVATE
    AI_FRONTIER_VERSION="${PROJECT_VERSION}"
)

target_include_directories(ai_frontier_backend_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    common
    config
    http
    runtime
    native
)

target_include_directories(ai_frontier_native_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    common
    config
    http
    native
)

target_include_directories(ai_frontier_runtime_module PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    common
    config
    http
    native
)

target_link_libraries(ai_frontier_backend_api PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_libraries(ai_frontier_native_worker PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_libraries(ai_frontier_runtime_module PRIVATE
    Qt6::Core
    Qt6::Network
)

set_target_properties(ai_frontier_backend_api ai_frontier_native_worker ai_frontier_runtime_module PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_dependencies(ai_frontier_backend_api ai_frontier_native_worker ai_frontier_runtime_module)
