import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier
import AiFrontier.Backend

GlassPanel {
    id: sidebar

    width: collapsed ? 82 : 264
    radius: 14
    clip: true

    required property int currentPage
    property bool collapsed: false
    property bool advancedMode: false

    signal pageSelected(int index)
    signal modeSelected(bool value)
    signal toggleCollapseRequested()

    readonly property var fullState: AppController.fullState || ({})
    readonly property var config: fullState["config"] !== undefined ? fullState["config"] : ({})
    readonly property var datasets: config["datasets"] !== undefined ? config["datasets"] : ({})
    readonly property var hardware: fullState["hardware"] !== undefined ? fullState["hardware"] : ({})
    readonly property var feedRows: fullState["feed"] !== undefined ? fullState["feed"] : []

    readonly property var viewItems: [
        { icon: "\u25c8", label: "Dashboard", index: 0 },
        { icon: "\u25f3", label: "Metrics", index: 1 },
        { icon: "\u2699", label: "Settings", index: 2 },
        { icon: "\u25a3", label: "Data", index: 3 }
    ]
    readonly property var operationItems: [
        { icon: "\u2630", label: "Jobs", index: 4 },
        { icon: "\u25ce", label: "Chat", index: 5 },
        { icon: "\u25a5", label: "Reports", index: 6 }
    ]
    readonly property var runtimeItems: [
        { icon: "\u25c9", label: "Live Feed", index: 7 },
        { icon: "\u2691", label: "Diagnostics", index: 8 }
    ]

    Behavior on width {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    function compactGpuName(value) {
        var text = String(value || "")
        if (text.length === 0) return "--"
        return text.replace("NVIDIA ", "").replace("GeForce ", "")
    }

    function quickBadgeColor(value) {
        if (value === "Live") return AppTheme.success
        if (value === "--") return AppTheme.textMuted
        return AppTheme.accentSecondary
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 14
        clip: true
        background: null
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 6
            padding: 0
            background: Rectangle { implicitWidth: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.04) }
            contentItem: Rectangle { implicitWidth: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.18) }
        }

        Column {
            width: sidebar.width - 28
            spacing: 12

            Rectangle {
                width: parent.width
                height: sidebar.collapsed ? 118 : 136
                radius: 12
                color: AppTheme.alpha(AppTheme.panelAlt, 0.44)
                border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: sidebar.collapsed ? 12 : 14
                    spacing: 14
                    visible: !sidebar.collapsed

                    Row {
                        width: parent.width
                        spacing: 12

                        Rectangle {
                            width: 54
                            height: 54
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.color: Qt.rgba(1, 1, 1, 0.12)
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "A"
                                color: AppTheme.textPrimary
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }

                            Rectangle {
                                width: 13
                                height: 13
                                radius: 6.5
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                color: AppController.connected ? AppTheme.success : AppTheme.danger
                                border.color: AppTheme.windowBase
                                border.width: 2
                            }
                        }

                        Column {
                            width: parent.width - 112
                            spacing: 4

                            Text {
                                text: "AI Frontier"
                                color: AppTheme.textPrimary
                                font.pixelSize: 17
                                font.weight: Font.Bold
                                width: parent.width
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "Operations deck"
                                color: AppTheme.textMuted
                                font.pixelSize: 11
                                width: parent.width
                                elide: Text.ElideRight
                            }

                            Text {
                                text: AppController.connected ? "Backend linked" : "Waiting for backend"
                                color: AppController.connected ? AppTheme.success : AppTheme.danger
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            width: 36
                            height: 36
                            radius: 8
                            color: collapseHover.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                            border.color: Qt.rgba(1, 1, 1, collapseHover.containsMouse ? 0.10 : 0.06)
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "\u25c0"
                                color: AppTheme.textSecondary
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: collapseHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: sidebar.toggleCollapseRequested()
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 40
                        radius: 0
                        color: Qt.rgba(1, 1, 1, 0.025)
                        border.color: Qt.rgba(1, 1, 1, 0.07)
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 3

                            Repeater {
                                model: [
                                    { label: "Basic", advanced: false },
                                    { label: "Expert", advanced: true }
                                ]

                                Rectangle {
                                    required property var modelData
                                    width: (parent.width - 4) / 2
                                    height: parent.height
                                    radius: 0
                                    color: sidebar.advancedMode === modelData.advanced
                                        ? AppTheme.alpha(AppTheme.accentPrimary, 0.11)
                                        : "transparent"
                                    border.color: sidebar.advancedMode === modelData.advanced
                                        ? AppTheme.alpha(AppTheme.accentPrimary, 0.16)
                                        : "transparent"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: parent.modelData.label
                                        color: sidebar.advancedMode === parent.modelData.advanced ? AppTheme.textPrimary : AppTheme.textSecondary
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: sidebar.modeSelected(modelData.advanced)
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    spacing: 14
                    visible: sidebar.collapsed

                    Rectangle {
                        width: 50
                        height: 50
                        radius: 10
                        color: Qt.rgba(1, 1, 1, 0.05)
                        border.color: Qt.rgba(1, 1, 1, 0.12)
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "A"
                            color: AppTheme.textPrimary
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }

                        Rectangle {
                            width: 13
                            height: 13
                            radius: 6.5
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            color: AppController.connected ? AppTheme.success : AppTheme.danger
                            border.color: AppTheme.windowBase
                            border.width: 2
                        }
                    }

                    Rectangle {
                        width: 38
                        height: 36
                        radius: 8
                        color: expandHover.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        border.color: Qt.rgba(1, 1, 1, expandHover.containsMouse ? 0.10 : 0.06)
                        border.width: 1
                        anchors.horizontalCenter: parent.horizontalCenter

                        Text {
                            anchors.centerIn: parent
                            text: "\u25b6"
                            color: AppTheme.textSecondary
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: expandHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: sidebar.toggleCollapseRequested()
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: "VIEWS"
                    visible: !sidebar.collapsed
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    leftPadding: 8
                }

                Repeater {
                    model: sidebar.viewItems
                    NavButton {
                        width: parent.width
                        iconText: modelData.icon
                        label: modelData.label
                        active: sidebar.currentPage === modelData.index
                        collapsed: sidebar.collapsed
                        onClicked: sidebar.pageSelected(modelData.index)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: "OPERATIONS"
                    visible: !sidebar.collapsed
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    leftPadding: 8
                }

                Repeater {
                    model: sidebar.operationItems
                    NavButton {
                        width: parent.width
                        iconText: modelData.icon
                        label: modelData.label
                        active: sidebar.currentPage === modelData.index
                        collapsed: sidebar.collapsed
                        onClicked: sidebar.pageSelected(modelData.index)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: "RUNTIME"
                    visible: !sidebar.collapsed
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    leftPadding: 8
                }

                Repeater {
                    model: sidebar.runtimeItems
                    NavButton {
                        width: parent.width
                        iconText: modelData.icon
                        label: modelData.label
                        active: sidebar.currentPage === modelData.index
                        collapsed: sidebar.collapsed
                        onClicked: sidebar.pageSelected(modelData.index)
                    }
                }
            }

            Column {
                width: parent.width
                visible: !sidebar.collapsed
                spacing: 9

                Text {
                    text: "QUICK ACCESS"
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    leftPadding: 8
                }

                Repeater {
                    model: [
                        { label: "Dataset Info", value: datasets["max_samples"] !== undefined ? Math.max(1, Math.round(Number(datasets["max_samples"]) / 1000)) + "k" : "--" },
                        { label: "System Resources", value: hardware["gpu_utilization"] !== undefined ? Math.round(Number(hardware["gpu_utilization"])) + "%" : "--" },
                        { label: "Training Logs", value: feedRows.length > 0 ? "Live" : "Idle" }
                    ]

                    Rectangle {
                        required property var modelData
                        width: parent.width
                        height: 48
                        radius: 10
                        color: Qt.rgba(1, 1, 1, 0.03)
                        border.color: Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 12
                            spacing: 8

                            Text {
                                id: quickLabel
                                text: parent.parent.modelData.label
                                color: AppTheme.textSecondary
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                width: Math.max(0, parent.width - quickBadge.width - 28)
                            }

                            Item {
                                width: Math.max(0, parent.width - quickLabel.width - quickBadge.width - 8)
                                height: 1
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FlatTag {
                                id: quickBadge
                                text: parent.parent.modelData.value
                                tagColor: sidebar.quickBadgeColor(parent.parent.modelData.value)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                visible: !sidebar.collapsed
                spacing: 9

                Text {
                    text: "SYSTEM"
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    leftPadding: 8
                }

                Rectangle {
                    width: parent.width
                    radius: 10
                    color: Qt.rgba(1, 1, 1, 0.03)
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 1
                    implicitHeight: systemColumn.implicitHeight + 22

                    Column {
                        id: systemColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Repeater {
                            model: [
                                { label: "GPU", value: sidebar.compactGpuName(hardware["gpu_name"]) },
                                { label: "Load", value: hardware["gpu_utilization"] !== undefined ? Math.round(Number(hardware["gpu_utilization"])) + "%" : "--" },
                                { label: "RAM", value: (hardware["ram_total_mb"] !== undefined && Number(hardware["ram_total_mb"]) > 0) ? (Number(hardware["ram_used_mb"] || 0) / 1024).toFixed(1) + " / " + (Number(hardware["ram_total_mb"] || 0) / 1024).toFixed(1) + " GB" : "--" }
                            ]

                            Row {
                                required property var modelData
                                width: parent.width
                                spacing: 8

                                Rectangle {
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: parent.modelData.label === "Load" ? AppTheme.accentPrimary : AppTheme.success
                                }

                                Text {
                                    id: systemLabel
                                    text: parent.modelData.label
                                    color: AppTheme.textMuted
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Item { width: Math.max(0, parent.width - systemLabel.implicitWidth - systemValue.implicitWidth - 24); height: 1 }

                                Text {
                                    id: systemValue
                                    text: parent.modelData.value
                                    color: AppTheme.textPrimary
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 6 }
        }
    }
}
