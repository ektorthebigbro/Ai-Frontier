cmake_minimum_required(VERSION 3.16)
project(ai_frontier_qt_dashboard VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 QuickEffects ShaderTools Network)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

# ── Main GUI application ────────────────────────────────────────────────────
qt_add_executable(ai_frontier_qt_dashboard  # Remove WIN32 temporarily for console debug output
    src/main.cpp
    src/app/ApiClient.h
    src/app/ApiClient.cpp
    src/app/AppController.h
    src/app/AppController.cpp
)

# Mark AppTheme as a QML singleton so the generated qmldir registers it correctly
set_source_files_properties(src/qml/components/AppTheme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(ai_frontier_qt_dashboard
    URI AiFrontier
    VERSION 1.0
    RESOURCES
        assets/bg/dark_bg.jpg
    QML_FILES
        src/qml/main.qml
        src/qml/components/AmbientBackdrop.qml
        src/qml/components/AppTheme.qml
        src/qml/components/GlassActionButton.qml
        src/qml/components/GlassPanel.qml
        src/qml/components/GlassStatTile.qml
        src/qml/components/GlowCard.qml
        src/qml/components/GradientProgressBar.qml
        src/qml/components/HeroMetricCard.qml
        src/qml/components/TrendChartWidget.qml
        src/qml/components/DataSplitBar.qml
        src/qml/components/ResourceBar.qml
        src/qml/components/StatusIndicator.qml
        src/qml/components/PillBadge.qml
        src/qml/components/IconBadge.qml
        src/qml/components/SidebarNav.qml
        src/qml/components/NavButton.qml
        src/qml/components/PageIntro.qml
        src/qml/components/SectionHeader.qml
        src/qml/components/ToastNotification.qml
        src/qml/pages/OverviewPage.qml
        src/qml/pages/MetricsPage.qml
        src/qml/pages/DataPage.qml
        src/qml/pages/JobsPage.qml
        src/qml/pages/jobs/JobWorkflowCard.qml
        src/qml/pages/SettingsPage.qml
        src/qml/pages/ChatPage.qml
        src/qml/pages/DiagnosticsPage.qml
        src/qml/pages/diagnostics/DiagnosticsHealthRow.qml
        src/qml/pages/diagnostics/DiagnosticsIssueCard.qml
        src/qml/pages/LiveFeedPage.qml
        src/qml/pages/ReportsPage.qml
)

target_include_directories(ai_frontier_qt_dashboard PRIVATE
    src/app
    src/style
    src/util
)

qt_add_shaders(ai_frontier_qt_dashboard "ai_frontier_shaders"
    BATCHABLE
    PRECOMPILE
    OPTIMIZED
    PREFIX
        /qt/qml/AiFrontier/shaders
    BASE
        shaders
    FILES
        shaders/pass.vert
        shaders/frosted_glass.frag
)

target_link_libraries(ai_frontier_qt_dashboard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::QuickEffects
    Qt6::Network
)

if(WIN32)
    target_link_libraries(ai_frontier_qt_dashboard PRIVATE dwmapi)
endif()
