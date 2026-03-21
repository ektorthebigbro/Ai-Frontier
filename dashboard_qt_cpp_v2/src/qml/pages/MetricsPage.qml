import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    background: null

    property int chartRange: 167
    readonly property var state: AppController.fullState || ({})
    readonly property var jobs: state["jobs"] || ({})
    readonly property string reportText: state["report"] || ""
    readonly property string trainingMessage: jobs["training"] && jobs["training"]["message"] ? jobs["training"]["message"] : ""
    readonly property string evaluateMessage: jobs["evaluate"] && jobs["evaluate"]["message"] ? jobs["evaluate"]["message"] : ""

    function capture(text, regex) {
        var match = String(text || "").match(regex)
        return match && match.length > 1 ? match[1] : ""
    }

    function sliceSeries(series) {
        if (!series || chartRange <= 0 || series.length <= chartRange) return series || []
        return series.slice(series.length - chartRange)
    }

    function reportMetric() {
        if (!reportText || !reportText.trim().length) return "--"
        try {
            var parsed = JSON.parse(reportText)
            if (parsed["protocol_overall_score"] !== undefined) return Number(parsed["protocol_overall_score"]).toFixed(2)
            if (parsed["reasoning_score"] !== undefined) return Number(parsed["reasoning_score"]).toFixed(2)
            if (parsed["gsm8k_accuracy"] !== undefined) return (Number(parsed["gsm8k_accuracy"]) * 100).toFixed(1) + "%"
        } catch (err) {}
        return "--"
    }

    function bestOr(series, fn) {
        return series && series.length ? fn.apply(null, series) : NaN
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
                title: "Training Metrics"
                subtitle: "Use this surface to read the health of the run at a glance, then drop into the curves that explain it."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1300 ? 5 : 1
                columnSpacing: 16
                rowSpacing: 16

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1300 ? 3 : 1
                    implicitHeight: heroColumn.implicitHeight + 44
                    title: "MODEL HEALTH"
                    badge: evaluateMessage.length ? "evaluation attached" : "training live"
                    badgeColor: evaluateMessage.length ? AppTheme.chartC : AppTheme.accentPrimary

                    ColumnLayout {
                        id: heroColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 14

                        Text {
                            Layout.fillWidth: true
                            text: "The run is " + (AppController.trainLoss > 0 ? "streaming live metrics from the active training loop." : "waiting for fresh loss samples from the backend.")
                            color: AppTheme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { label: "Latest step", value: capture(trainingMessage, /step\s+(\d+)/i) || "--" },
                                    { label: "Current epoch", value: capture(trainingMessage, /epoch\s+(\d+\s*\/\s*\d+)/i) || "--" },
                                    { label: "Latest report", value: reportMetric() },
                                    { label: "Eval stream", value: evaluateMessage.length ? "Active" : "Idle" }
                                ]

                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 62
                                    radius: 14
                                    color: AppTheme.alpha(AppTheme.panelAlt, 0.52)
                                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
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
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1300 ? 2 : 1
                    implicitHeight: railColumn.implicitHeight + 44
                    title: "SIGNAL SNAPSHOT"

                    Column {
                        id: railColumn
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: [
                                { label: "Train loss", value: AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--", accent: AppTheme.chartD },
                                { label: "Validation loss", value: AppController.valLoss > 0 ? AppController.valLoss.toFixed(4) : "--", accent: AppTheme.accentSecondary },
                                { label: "Reasoning score", value: AppController.accuracy > 0 ? (AppController.accuracy * 100).toFixed(1) + "%" : "--", accent: AppTheme.success }
                            ]

                            Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 64
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
                                        font.letterSpacing: 0.8
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

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1150 ? 4 : 2
                columnSpacing: 12
                rowSpacing: 12

                Repeater {
                    model: [
                        { title: "TRAINING LOSS", value: AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--", chip: capture(trainingMessage, /step\s+(\d+)/i) ? "Step " + capture(trainingMessage, /step\s+(\d+)/i) : "Training stream", icon: "\u2198", iconColor: AppTheme.chartD },
                        { title: "VALIDATION LOSS", value: AppController.valLoss > 0 ? AppController.valLoss.toFixed(4) : "--", chip: capture(trainingMessage, /epoch\s+(\d+)/i) ? "Epoch " + capture(trainingMessage, /epoch\s+(\d+)/i) : "Validation stream", icon: "\u2198", iconColor: AppTheme.accentSecondary },
                        { title: "REASONING SCORE", value: AppController.accuracy > 0 ? (AppController.accuracy * 100).toFixed(1) + "%" : "--", chip: "Judge stream", icon: "\u2197", iconColor: AppTheme.success },
                        { title: "EVALUATION SCORE", value: reportMetric(), chip: evaluateMessage.length ? "Evaluation stream" : "Latest report", icon: "\u25c8", iconColor: AppTheme.chartB }
                    ]

                    HeroMetricCard {
                        required property var modelData
                        Layout.fillWidth: true
                        title: modelData.title
                        value: modelData.value
                        chip: modelData.chip
                        icon: modelData.icon
                        iconColor: modelData.iconColor
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                SectionHeader {
                    Layout.fillWidth: true
                    title: "Curve Analysis"
                    subtitle: "Flip the time window to see where instability starts and where performance settles."
                }

                Rectangle {
                    height: 34
                    width: rangeRow.implicitWidth + 8
                    radius: 10
                    color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                    border.width: 1

                    Row {
                        id: rangeRow
                        anchors.centerIn: parent
                        spacing: 2

                        Repeater {
                            model: [{ label: "1m", points: 33 }, { label: "5m", points: 167 }, { label: "30m", points: 1000 }, { label: "1h", points: 2000 }, { label: "All", points: 0 }]

                            Rectangle {
                                required property var modelData
                                height: 26
                                width: rangeLabel.width + 18
                                radius: 7
                                color: page.chartRange === modelData.points ? AppTheme.alpha(AppTheme.accentPrimary, 0.16) : "transparent"
                                border.color: page.chartRange === modelData.points ? AppTheme.alpha(AppTheme.accentPrimary, 0.24) : "transparent"
                                border.width: 1

                                Text {
                                    id: rangeLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: page.chartRange === modelData.points ? AppTheme.textPrimary : AppTheme.textMuted
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                }

                                MouseArea { anchors.fill: parent; onClicked: page.chartRange = modelData.points }
                            }
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1280 ? 3 : 1
                columnSpacing: 14
                rowSpacing: 14

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 2 : 1
                    Layout.preferredHeight: 330
                    title: "LOSS OVER TIME"

                    TrendChartWidget {
                        width: parent.width
                        height: 258
                        series1: page.sliceSeries(AppController.trainLossHistory)
                        series2: page.sliceSeries(AppController.valLossHistory)
                        label1: "Train"
                        label2: "Validation"
                        color1: AppTheme.chartD
                        color2: AppTheme.accentSecondary
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 330
                    title: "BEST WINDOWS"

                    Column {
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: [
                                { label: "Best train loss", value: isNaN(bestOr(AppController.trainLossHistory, Math.min)) ? "--" : bestOr(AppController.trainLossHistory, Math.min).toFixed(4), accent: AppTheme.chartD },
                                { label: "Best validation loss", value: isNaN(bestOr(AppController.valLossHistory, Math.min)) ? "--" : bestOr(AppController.valLossHistory, Math.min).toFixed(4), accent: AppTheme.accentSecondary },
                                { label: "Peak reasoning score", value: isNaN(bestOr(AppController.accuracyHistory, Math.max)) ? "--" : (bestOr(AppController.accuracyHistory, Math.max) * 100).toFixed(1) + "%", accent: AppTheme.success },
                                { label: "Observed steps", value: AppController.trainLossHistory ? String(AppController.trainLossHistory.length) : "--", accent: AppTheme.accentPrimary }
                            ]

                            Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 56
                                radius: 12
                                color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.08)
                                border.color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.16)
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: modelData.accent
                                    }

                                    Text {
                                        id: bestLabel
                                        text: modelData.label
                                        color: AppTheme.textSecondary
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Item { width: Math.max(0, parent.width - bestLabel.width - bestValue.width - 26); height: 1 }

                                    Text {
                                        id: bestValue
                                        text: modelData.value
                                        color: AppTheme.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    title: "ACCURACY OVER TIME"

                    TrendChartWidget {
                        width: parent.width
                        height: 228
                        series1: page.sliceSeries(AppController.accuracyHistory)
                        label1: "Reasoning"
                        color1: AppTheme.success
                        autoRange: false
                        minVal: 0
                        maxVal: 1
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 2 : 1
                    Layout.preferredHeight: 300
                    title: "SECONDARY SIGNALS"

                    TrendChartWidget {
                        width: parent.width
                        height: 228
                        series1: page.sliceSeries(AppController.rewardHistory)
                        series2: page.sliceSeries(AppController.progressHistory)
                        label1: "Reward"
                        label2: "Progress"
                        color1: AppTheme.chartC
                        color2: AppTheme.chartB
                    }
                }
            }
        }
    }
}
