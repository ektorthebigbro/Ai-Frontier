import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

Item {
    id: page

    readonly property var feedRows: AppController.fullState["feed"] || []
    property string filterJob: "all"

    function jobColor(job) {
        var j = String(job || "").toLowerCase()
        if (j === "training") return AppTheme.chartD
        if (j === "evaluate") return AppTheme.accentSecondary
        if (j === "inference") return AppTheme.chartB
        if (j === "autopilot") return AppTheme.accentPrimary
        if (j === "prepare") return AppTheme.success
        if (j === "setup") return AppTheme.chartA
        return AppTheme.textMuted
    }

    function jobBgColor(job) {
        var j = String(job || "").toLowerCase()
        if (j === "training") return AppTheme.alpha(AppTheme.chartD, 0.12)
        if (j === "evaluate") return AppTheme.alpha(AppTheme.accentSecondary, 0.12)
        if (j === "inference") return AppTheme.alpha(AppTheme.chartB, 0.12)
        if (j === "autopilot") return AppTheme.alpha(AppTheme.accentPrimary, 0.12)
        if (j === "prepare") return AppTheme.alpha(AppTheme.success, 0.12)
        if (j === "setup") return AppTheme.alpha(AppTheme.chartA, 0.12)
        return Qt.rgba(1, 1, 1, 0.04)
    }

    function filteredRows() {
        if (filterJob === "all") return feedRows
        return feedRows.filter(function(r) {
            return (r["job"] || r["type"] || "") === filterJob
        })
    }

    function jobLabels() {
        var seen = {}
        for (var i = 0; i < feedRows.length; ++i) {
            var j = feedRows[i]["job"] || feedRows[i]["type"] || ""
            if (j) seen[j] = true
        }
        return Object.keys(seen)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        PageIntro {
            Layout.fillWidth: true
            title: "Live Feed"
            subtitle: "Watch the backend stream in real time, isolate one workflow, and keep an eye on how fast events are arriving."
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 1180 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 12

            Repeater {
                model: [
                    { label: "TOTAL EVENTS", value: String(feedRows.length), accent: AppTheme.accentPrimary },
                    { label: "VISIBLE EVENTS", value: String(filteredRows().length), accent: AppTheme.chartB },
                    { label: "ACTIVE FILTER", value: filterJob === "all" ? "All jobs" : filterJob, accent: AppTheme.success },
                    { label: "STREAM STATE", value: feedRows.length ? "Live" : "Idle", accent: feedRows.length ? AppTheme.success : AppTheme.textMuted }
                ]

                GlassStatTile {
                    required property var modelData
                    Layout.fillWidth: true
                    height: 86
                    label: modelData.label
                    value: modelData.value
                    accent: modelData.accent
                    valueSize: 22
                }
            }
        }

        GlowCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "EVENT STREAM"
            badge: feedRows.length ? "live" : "waiting"
            badgeColor: feedRows.length ? AppTheme.success : AppTheme.textMuted

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    SectionHeader {
                        Layout.fillWidth: true
                        title: "Stream Controls"
                        subtitle: "Filter by workflow and clear the queue when you want a fresh operational view."
                    }

                    GlassActionButton {
                        text: "Clear Feed"
                        muted: true
                        onClicked: AppController.postActionPath("/api/feed/clear")
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: filterWrap.implicitHeight + 18
                    radius: 14
                    color: AppTheme.alpha(AppTheme.panelAlt, 0.50)
                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                    border.width: 1

                    Flow {
                        id: filterWrap
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        Repeater {
                            model: ["all"].concat(page.jobLabels())

                            Rectangle {
                                required property var modelData
                                readonly property bool active: page.filterJob === modelData
                                height: 28
                                width: filterLbl.width + 22
                                radius: 9
                                color: active ? AppTheme.alpha(AppTheme.accentPrimary, 0.16) : "transparent"
                                border.color: active ? AppTheme.alpha(AppTheme.accentPrimary, 0.24) : AppTheme.alpha(AppTheme.accentGlass, 0.05)
                                border.width: 1

                                Text {
                                    id: filterLbl
                                    anchors.centerIn: parent
                                    text: modelData === "all" ? "All jobs" : modelData
                                    color: active ? AppTheme.textPrimary : AppTheme.textMuted
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                }

                                MouseArea { anchors.fill: parent; onClicked: page.filterJob = modelData }
                            }
                        }
                    }
                }

                ListView {
                    id: feedList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: page.filteredRows()
                    displayMarginEnd: 8

                    onCountChanged: Qt.callLater(function() { feedList.positionViewAtEnd() })

                    delegate: Rectangle {
                        required property var modelData

                        property string jobName: modelData["job"] || modelData["type"] || "event"
                        property string stageStr: modelData["stage"] || ""
                        property string msgStr: modelData["message"] || ""
                        property string stampStr: modelData["ts"]
                            ? Qt.formatDateTime(new Date(Number(modelData["ts"]) * 1000), "HH:mm:ss")
                            : "--:--:--"

                        width: feedList.width
                        height: messageText.implicitHeight + 30
                        radius: 14
                        color: AppTheme.alpha(AppTheme.panelAlt, 0.46)
                        border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    text: stampStr
                                    color: AppTheme.textDim
                                    font.family: "Courier New"
                                    font.pixelSize: 10
                                }

                                Rectangle {
                                    height: 20
                                    width: Math.max(56, jobBadgeLabel.implicitWidth + 14)
                                    radius: 6
                                    color: page.jobBgColor(jobName)
                                    border.color: page.jobColor(jobName)
                                    border.width: 1

                                    Text {
                                        id: jobBadgeLabel
                                        anchors.centerIn: parent
                                        text: jobName
                                        color: page.jobColor(jobName)
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.4
                                    }
                                }

                                Text {
                                    visible: stageStr.length > 0
                                    text: stageStr
                                    color: AppTheme.textMuted
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                id: messageText
                                width: parent.width
                                text: msgStr
                                color: AppTheme.textSecondary
                                font.pixelSize: 11
                                font.family: "Courier New"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Text {
                        visible: feedList.count === 0
                        anchors.centerIn: parent
                        text: feedRows.length ? "No events match the current filter." : "Waiting for backend events..."
                        color: AppTheme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
