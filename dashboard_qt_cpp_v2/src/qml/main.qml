import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AiFrontier
import AiFrontier.Backend

ApplicationWindow {
    id: root
    visible: true
    width: 1540
    height: 980
    minimumWidth: 1240
    minimumHeight: 760
    title: "AI Frontier - Training Control Center"
    flags: Qt.FramelessWindowHint | Qt.Window
    color: AppTheme.windowBase

    property int currentPage: 0
    property int displayedPage: 0
    property int pendingPage: -1
    property bool advancedMode: false
    property bool sidebarCollapsed: false
    property point dragStart: Qt.point(0, 0)

    readonly property var fullState: AppController.fullState || ({})
    readonly property var primaryJob: fullState["primary_job"] !== undefined ? fullState["primary_job"] : ({})
    readonly property string currentStage: primaryJob["stage"] !== undefined ? String(primaryJob["stage"]) : "idle"
    readonly property string statusMessage: !AppController.connected
        ? (AppController.connectionError.length ? AppController.connectionError : "Waiting for backend")
        : (primaryJob["message"] !== undefined ? String(primaryJob["message"]) : "Waiting for activity")
    readonly property string statusMeta: {
        if (!AppController.connected)
            return "Reconnecting | " + relativeTime(primaryJob["updated_at"] !== undefined ? Number(primaryJob["updated_at"]) : 0)
        var pct = progressPct(primaryJob["progress"] !== undefined ? Number(primaryJob["progress"]) : 0)
        var eta = primaryJob["eta"] !== undefined && String(primaryJob["eta"]).length > 0
            ? String(primaryJob["eta"])
            : "ETA unavailable"
        var updated = relativeTime(primaryJob["updated_at"] !== undefined ? Number(primaryJob["updated_at"]) : 0)
        return pct + " | " + eta + " | " + updated
    }

    function prettyJobName(job) {
        var value = String(job || "")
        if (value === "setup") return "Environment Setup"
        if (value === "prepare") return "Prepare Data"
        if (value === "training") return "Training"
        if (value === "evaluate") return "Evaluation"
        if (value === "inference") return "Inference"
        if (value === "autopilot") return "Autopilot"
        if (value.length === 0) return "Ready"
        return value.charAt(0).toUpperCase() + value.slice(1).replace(/_/g, " ")
    }

    function progressPct(progress) {
        var value = Math.max(0, Math.min(1, Number(progress || 0)))
        return (value * 100).toFixed(1) + "%"
    }

    function relativeTime(ts) {
        var raw = Number(ts || 0)
        if (!raw || raw <= 0) return "Updated recently"
        var delta = Math.max(0, Math.floor(Date.now() / 1000) - raw)
        if (delta < 60) return "Updated just now"
        if (delta < 3600) return "Updated " + Math.floor(delta / 60) + "m ago"
        if (delta < 86400) return "Updated " + Math.floor(delta / 3600) + "h ago"
        return "Updated " + Math.floor(delta / 86400) + "d ago"
    }

    function switchPage(index) {
        if (index === currentPage && pendingPage < 0)
            return
        currentPage = index
        pendingPage = index
        pageTransition.restart()
    }

    Component.onCompleted: {
        root.x = (Screen.width - width) / 2
        root.y = (Screen.height - height) / 2
        displayedPage = currentPage
    }

    AmbientBackdrop {
        anchors.fill: parent
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
    }

    GlassPanel {
        id: chromeBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        height: 64
        radius: 18
        clip: true
        z: 20

        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) { root.dragStart = Qt.point(mouse.x, mouse.y) }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                root.x += mouse.x - root.dragStart.x
                root.y += mouse.y - root.dragStart.y
            }
            onDoubleClicked: {
                if (root.visibility === Window.Maximized)
                    root.showNormal()
                else
                    root.showMaximized()
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 14
            spacing: 16

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 14

                Rectangle {
                    width: 38
                    height: 38
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.08)
                    border.color: Qt.rgba(1, 1, 1, 0.18)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "A"
                        color: AppTheme.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }
                }

                ColumnLayout {
                    spacing: 1

                    Text {
                        text: "AI Frontier"
                        color: AppTheme.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }

                    Text {
                        text: "Training control center"
                        color: AppTheme.textMuted
                        font.pixelSize: 11
                    }
                }
            }

            Item { Layout.fillWidth: true }

            StatusIndicator {
                Layout.alignment: Qt.AlignVCenter
                connected: AppController.connected
                label: AppController.connected ? "Backend linked" : "Backend offline"
            }

            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 6

                Repeater {
                    model: [
                        { label: "\u2013", action: "min" },
                        { label: "\u25a1", action: "max" },
                        { label: "\u2715", action: "close", danger: true }
                    ]

                    Rectangle {
                        required property var modelData
                        width: 38
                        height: 36
                        radius: 10
                        color: controlArea.containsMouse
                            ? (modelData.danger ? Qt.rgba(0.94, 0.44, 0.42, 0.18) : Qt.rgba(1, 1, 1, 0.07))
                            : "transparent"
                        border.color: controlArea.containsMouse
                            ? (modelData.danger ? Qt.rgba(0.94, 0.44, 0.42, 0.28) : Qt.rgba(1, 1, 1, 0.10))
                            : "transparent"
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 120 } }
                        Behavior on border.color { ColorAnimation { duration: 120 } }

                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData.label
                            color: parent.modelData.danger ? AppTheme.danger : AppTheme.textSecondary
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: controlArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (parent.modelData.action === "min") {
                                    root.showMinimized()
                                } else if (parent.modelData.action === "max") {
                                    if (root.visibility === Window.Maximized)
                                        root.showNormal()
                                    else
                                        root.showMaximized()
                                } else {
                                    root.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Row {
        anchors.top: chromeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 16
        anchors.bottomMargin: 16
        spacing: 16

        SidebarNav {
            id: sidebar
            height: parent.height
            currentPage: root.currentPage
            collapsed: root.sidebarCollapsed
            advancedMode: root.advancedMode
            onPageSelected: function(index) { root.switchPage(index) }
            onModeSelected: function(value) { root.advancedMode = value }
            onToggleCollapseRequested: root.sidebarCollapsed = !root.sidebarCollapsed
        }

        GlassPanel {
            id: pageShell
            width: parent.width - sidebar.width - parent.spacing
            height: parent.height
            radius: 24
            clip: true

            Item {
                id: pageViewport
                anchors.fill: parent
                anchors.margins: 22
                clip: true
                opacity: 1.0

                SwipeView {
                    id: pageStack
                    anchors.fill: parent
                    currentIndex: root.displayedPage
                    interactive: false
                    clip: true
                    background: null

                    OverviewPage { advancedMode: root.advancedMode }
                    MetricsPage { }
                    SettingsPage { advancedMode: root.advancedMode }
                    DataPage { }
                    JobsPage { }
                    ChatPage { }
                    ReportsPage { }
                    LiveFeedPage { }
                    DiagnosticsPage { }
                }
            }
        }
    }

    SequentialAnimation {
        id: pageTransition
        NumberAnimation {
            target: pageViewport
            property: "opacity"
            to: 0.0
            duration: 90
            easing.type: Easing.InOutQuad
        }
        ScriptAction {
            script: {
                if (root.pendingPage >= 0) {
                    root.displayedPage = root.pendingPage
                    root.pendingPage = -1
                }
            }
        }
        PauseAnimation { duration: 24 }
        NumberAnimation {
            target: pageViewport
            property: "opacity"
            to: 1.0
            duration: 170
            easing.type: Easing.OutCubic
        }
    }

    GlassPanel {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        height: 46
        radius: 14
        clip: true
        z: 20

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            StatusIndicator {
                connected: AppController.connected
                label: AppController.connected
                    ? (root.currentStage === "idle" ? "Ready" : root.prettyJobName(root.primaryJob["job"]))
                    : "Disconnected"
            }

            Text {
                text: root.statusMessage
                color: AppTheme.textSecondary
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.statusMeta
            color: AppTheme.textMuted
            font.pixelSize: 11
        }
    }

    ToastNotification {
        id: toast
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.rightMargin: 26
        anchors.bottomMargin: 16
        z: 40
    }

    Connections {
        target: AppController
        function onToastRequested(message, type) {
            toast.show(message, type)
        }
    }
}
