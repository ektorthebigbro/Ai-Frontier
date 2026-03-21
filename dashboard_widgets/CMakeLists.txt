cmake_minimum_required(VERSION 3.16)
project(ai_frontier_qt_dashboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(ai_frontier_qt_dashboard WIN32
    src/main.cpp
    src/app/DashboardWindow.h
    src/app/ApiClient.cpp
    src/app/DashboardWindowShell.cpp
    src/app/DashboardWindowEvents.cpp
    src/pages/BasePage.cpp
    src/pages/ChatPage.cpp
    src/pages/DataPage.cpp
    src/pages/DiagnosticsPage.cpp
    src/pages/JobsPage.cpp
    src/pages/LiveFeedPage.cpp
    src/pages/MetricsPage.cpp
    src/pages/OverviewPage.cpp
    src/pages/ReportsPage.cpp
    src/pages/SettingsPage.cpp
    src/style/StyleEngine.cpp
    src/widgets/DataSplitBar.cpp
    src/widgets/GlowCard.cpp
    src/widgets/GradientProgressBar.cpp
    src/widgets/HeroMetricCard.cpp
    src/widgets/IconBadge.cpp
    src/widgets/PillBadge.cpp
    src/widgets/ResourceBar.cpp
    src/widgets/StatusIndicator.cpp
    src/widgets/trend_chart_widget.cpp
)

set_target_properties(ai_frontier_qt_dashboard PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(ai_frontier_qt_dashboard PRIVATE
    src
    src/app
    src/pages
    src/style
    src/util
    src/widgets
)

target_link_libraries(ai_frontier_qt_dashboard PRIVATE
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(ai_frontier_qt_dashboard PRIVATE dwmapi)
endif()
