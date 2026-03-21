import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    background: null

    readonly property var state: AppController.fullState || ({})
    readonly property string reportText: state["report"] || ""
    readonly property var jobs: state["jobs"] || ({})

    function evalScore() {
        if (!reportText || !reportText.trim().length) return "--"
        try {
            var parsed = JSON.parse(reportText)
            if (parsed["protocol_overall_score"] !== undefined) return Number(parsed["protocol_overall_score"]).toFixed(2)
            if (parsed["reasoning_score"] !== undefined) return Number(parsed["reasoning_score"]).toFixed(2)
            if (parsed["gsm8k_accuracy"] !== undefined) return (Number(parsed["gsm8k_accuracy"]) * 100).toFixed(1) + "%"
        } catch (err) {}
        return "--"
    }

    function evalStage() {
        return String((jobs["evaluate"] || {})["stage"] || "idle")
    }

    function parsedMetrics() {
        if (!reportText || !reportText.trim().length) return []
        try {
            var parsed = JSON.parse(reportText)
            var rows = []
            var numericKeys = [
                "protocol_overall_score", "reasoning_score", "gsm8k_accuracy",
                "accuracy", "f1", "precision", "recall", "bleu",
                "perplexity", "avg_reward", "avg_loss"
            ]
            for (var i = 0; i < numericKeys.length; ++i) {
                var key = numericKeys[i]
                if (parsed[key] !== undefined) {
                    var raw = Number(parsed[key])
                    var display = key.endsWith("_accuracy") || key === "accuracy" || key === "f1" || key === "precision" || key === "recall"
                        ? (raw * 100).toFixed(1) + "%"
                        : raw.toFixed(4)
                    rows.push({ key: key.replace(/_/g, " "), value: display })
                }
            }
            for (var k in parsed) {
                if (typeof parsed[k] === "string" && !numericKeys.includes(k) && rows.length < 12)
                    rows.push({ key: k.replace(/_/g, " "), value: String(parsed[k]) })
            }
            return rows
        } catch (err) {}
        return []
    }

    function isJsonReport() {
        if (!reportText || !reportText.trim().length) return false
        try { JSON.parse(reportText); return true } catch (e) { return false }
    }

    Item {
        width: page.availableWidth
        implicitHeight: content.implicitHeight + 48

        ColumnLayout {
            id: content
            x: 20
            y: 22
            width: parent.width - 40
            spacing: 18

            PageIntro {
                Layout.fillWidth: true
                title: "Reports"
                subtitle: "Launch evaluations, scan benchmark highlights, and inspect the raw output without leaving the dashboard."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1280 ? 5 : 1
                columnSpacing: 16
                rowSpacing: 16

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 3 : 1
                    implicitHeight: briefColumn.implicitHeight + 44
                    title: "EVALUATION BRIEF"
                    badge: evalStage()
                    badgeColor: evalStage() === "completed" ? AppTheme.success : (evalStage() === "running" ? AppTheme.accentPrimary : AppTheme.textMuted)

                    ColumnLayout {
                        id: briefColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 14

                        Text {
                            Layout.fillWidth: true
                            text: reportText.length
                                ? "A report is available. Use the parsed metric grid for the headline numbers and the raw pane for exact payload inspection."
                                : "No evaluation report is loaded yet. Run a benchmark pass to populate this surface."
                            color: AppTheme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { label: "Eval score", value: evalScore(), accent: AppTheme.success },
                                    { label: "Stage", value: evalStage(), accent: AppTheme.accentPrimary },
                                    { label: "Metrics parsed", value: isJsonReport() ? String(parsedMetrics().length) : "0", accent: AppTheme.chartB }
                                ]

                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 66
                                    radius: 14
                                    color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.10)
                                    border.color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.18)
                                    border.width: 1

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 4

                                        Text {
                                            text: modelData.label
                                            color: AppTheme.textMuted
                                            font.pixelSize: 10
                                            font.weight: Font.Bold
                                            font.letterSpacing: 0.9
                                        }

                                        Text {
                                            text: modelData.value
                                            color: AppTheme.textPrimary
                                            font.pixelSize: 22
                                            font.weight: Font.Bold
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 2 : 1
                    implicitHeight: actionsColumn.implicitHeight + 44
                    title: "REPORT ACTIONS"

                    Column {
                        id: actionsColumn
                        width: parent.width
                        spacing: 10

                        Text {
                            width: parent.width
                            text: "Kick off a fresh evaluation run or stop the current one if you need to reclaim compute immediately."
                            color: AppTheme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        GlassActionButton {
                            width: parent.width
                            height: 42
                            text: "Run Evaluation"
                            iconText: "\u25b6"
                            primary: true
                            onClicked: AppController.generateReport()
                        }

                        GlassActionButton {
                            visible: evalStage() === "running"
                            width: parent.width
                            height: 42
                            text: "Stop Evaluation"
                            iconText: "\u25a0"
                            danger: true
                            onClicked: AppController.postActionPath("/api/actions/evaluate/stop")
                        }
                    }
                }
            }

            SectionHeader {
                Layout.fillWidth: true
                title: "Metric Grid"
                subtitle: isJsonReport()
                    ? "Parsed fields from the latest report payload."
                    : "This section fills automatically when the report body is valid JSON."
            }

            GridLayout {
                visible: isJsonReport() && parsedMetrics().length > 0
                Layout.fillWidth: true
                columns: Math.max(2, Math.min(4, Math.floor(width / 240)))
                columnSpacing: 12
                rowSpacing: 12

                Repeater {
                    model: parsedMetrics()
                    GlassStatTile {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 76
                        label: modelData.key.toUpperCase()
                        value: modelData.value
                        accent: AppTheme.accentPrimary
                        valueSize: 20
                    }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 480
                title: isJsonReport() ? "RAW JSON REPORT" : "EVALUATION REPORT"

                ScrollView {
                    anchors.fill: parent
                    clip: true

                    TextArea {
                        readOnly: true
                        text: reportText.length ? reportText : "No reports yet.\nRun an evaluation to populate this view."
                        color: AppTheme.textSecondary
                        background: null
                        font.family: "Courier New"
                        font.pixelSize: 12
                        wrapMode: isJsonReport() ? TextArea.WrapAnywhere : TextArea.Wrap
                        selectByMouse: true
                    }
                }
            }
        }
    }
}
