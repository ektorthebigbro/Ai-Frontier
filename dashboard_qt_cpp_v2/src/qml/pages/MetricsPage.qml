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
        if (!series || chartRange <= 0 || series.length <= chartRange)
            return series || []
        return series.slice(series.length - chartRange)
    }

    function reportMetric() {
        if (!reportText || !reportText.trim().length)
            return "--"
        try {
            var parsed = JSON.parse(reportText)
            if (parsed["protocol_overall_score"] !== undefined)
                return Number(parsed["protocol_overall_score"]).toFixed(2)
            if (parsed["reasoning_score"] !== undefined)
                return Number(parsed["reasoning_score"]).toFixed(2)
            if (parsed["gsm8k_accuracy"] !== undefined)
                return (Number(parsed["gsm8k_accuracy"]) * 100).toFixed(1) + "%"
        } catch (err) {}
        return "--"
    }

    function bestOr(series, fn) {
        return series && series.length ? fn.apply(null, series) : NaN
    }

    function lastOrNaN(series) {
        return series && series.length ? Number(series[series.length - 1]) : NaN
    }

    function delta(series) {
        if (!series || series.length < 2)
            return NaN
        return Number(series[series.length - 1]) - Number(series[series.length - 2])
    }

    function rollingMean(series, count) {
        if (!series || !series.length)
            return NaN
        var size = Math.min(series.length, Math.max(1, count))
        var start = series.length - size
        var total = 0
        for (var i = start; i < series.length; ++i)
            total += Number(series[i])
        return total / size
    }

    function averageDelta(series, count) {
        if (!series || series.length < 2)
            return NaN
        var size = Math.min(series.length - 1, Math.max(1, count))
        var start = series.length - 1 - size
        var total = 0
        for (var i = start + 1; i < series.length; ++i)
            total += Number(series[i]) - Number(series[i - 1])
        return total / size
    }

    function formatFixed(value, digits) {
        return isNaN(value) ? "--" : Number(value).toFixed(digits)
    }

    function formatPercent(value) {
        return isNaN(value) ? "--" : (Number(value) * 100).toFixed(1) + "%"
    }

    function formatSigned(value, digits) {
        if (isNaN(value))
            return "--"
        var number = Number(value)
        return (number >= 0 ? "+" : "") + number.toFixed(digits)
    }

    Item {
        width: page.availableWidth
        implicitHeight: content.implicitHeight + 44

        ColumnLayout {
            id: content
            x: 18
            y: 18
            width: parent.width - 36
            spacing: 16

            PageIntro {
                Layout.fillWidth: true
                title: "Training Metrics"
                subtitle: "Read the live signal path, inspect the rate math behind the run, and track where the curves are stabilizing or drifting."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: content.width > 1360 ? 6 : 1
                columnSpacing: 14
                rowSpacing: 14

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1360 ? 4 : 1
                    implicitHeight: liveComputeColumn.implicitHeight + 38
                    title: "LIVE COMPUTE"
                    badge: AppController.connected ? "backend stream" : "awaiting link"
                    badgeColor: AppController.connected ? AppTheme.accentPrimary : AppTheme.danger

                    ColumnLayout {
                        id: liveComputeColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 12

                        Text {
                            Layout.fillWidth: true
                            text: AppController.trainLoss > 0
                                ? "The dashboard is deriving motion and stability directly from the incoming metric history."
                                : "Waiting for fresh backend samples before derived math can be computed."
                            color: AppTheme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 760 ? 3 : 2
                            columnSpacing: 10
                            rowSpacing: 10

                            Repeater {
                                model: [
                                    {
                                        label: "Loss delta",
                                        value: page.formatSigned(page.delta(AppController.trainLossHistory), 4),
                                        detail: "Change since last loss sample",
                                        accent: AppTheme.chartD
                                    },
                                    {
                                        label: "Rolling mean",
                                        value: page.formatFixed(page.rollingMean(AppController.trainLossHistory, 16), 4),
                                        detail: "Mean of last 16 train samples",
                                        accent: AppTheme.accentPrimary
                                    },
                                    {
                                        label: "Validation drift",
                                        value: page.formatSigned(page.delta(AppController.valLossHistory), 4),
                                        detail: "Last validation step movement",
                                        accent: AppTheme.accentSecondary
                                    },
                                    {
                                        label: "Score slope",
                                        value: page.formatSigned(page.averageDelta(AppController.accuracyHistory, 12) * 100, 2) + (isNaN(page.averageDelta(AppController.accuracyHistory, 12)) ? "" : " pts"),
                                        detail: "Average reasoning change over 12 samples",
                                        accent: AppTheme.success
                                    },
                                    {
                                        label: "Reward mean",
                                        value: page.formatFixed(page.rollingMean(AppController.rewardHistory, 12), 3),
                                        detail: "Mean of recent reward observations",
                                        accent: AppTheme.chartC
                                    },
                                    {
                                        label: "Progress velocity",
                                        value: page.formatSigned(page.averageDelta(AppController.progressHistory, 10) * 100, 2) + (isNaN(page.averageDelta(AppController.progressHistory, 10)) ? "" : "%"),
                                        detail: "Average progress change per sample",
                                        accent: AppTheme.chartB
                                    }
                                ]

                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 84
                                    radius: 8
                                    color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.07)
                                    border.color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.16)
                                    border.width: 1

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 2
                                        color: modelData.accent
                                        opacity: 0.9
                                    }

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 4

                                        Text {
                                            text: modelData.label
                                            color: AppTheme.textMuted
                                            font.pixelSize: 10
                                            font.weight: Font.Bold
                                            font.letterSpacing: 1.1
                                        }

                                        Text {
                                            text: modelData.value
                                            color: AppTheme.textPrimary
                                            font.pixelSize: 22
                                            font.weight: Font.Bold
                                        }

                                        Text {
                                            text: modelData.detail
                                            color: AppTheme.textSecondary
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                            width: parent.width
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1360 ? 2 : 1
                    implicitHeight: contextColumn.implicitHeight + 38
                    title: "RUN CONTEXT"
                    badge: evaluateMessage.length ? "eval attached" : "training only"
                    badgeColor: evaluateMessage.length ? AppTheme.chartC : AppTheme.textMuted

                    Column {
                        id: contextColumn
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: [
                                { label: "Latest step", value: capture(trainingMessage, /step\s+(\d+)/i) || "--" },
                                { label: "Current epoch", value: capture(trainingMessage, /epoch\s+(\d+\s*\/\s*\d+)/i) || "--" },
                                { label: "Current train loss", value: AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--" },
                                { label: "Current validation", value: AppController.valLoss > 0 ? AppController.valLoss.toFixed(4) : "--" },
                                { label: "Reasoning score", value: AppController.accuracy > 0 ? (AppController.accuracy * 100).toFixed(1) + "%" : "--" },
                                { label: "Latest report", value: page.reportMetric() },
                                { label: "Eval stream", value: evaluateMessage.length ? "Active" : "Idle" },
                                { label: "Tracked samples", value: AppController.trainLossHistory ? String(AppController.trainLossHistory.length) : "--" }
                            ]

                            Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 40
                                radius: 8
                                color: Qt.rgba(1, 1, 1, 0.024)
                                border.color: Qt.rgba(1, 1, 1, 0.07)
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8

                                    Text {
                                        id: contextLabel
                                        text: parent.parent.modelData.label
                                        color: AppTheme.textMuted
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.8
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Item {
                                        width: Math.max(0, parent.width - contextLabel.width - contextValue.width - 8)
                                        height: 1
                                    }

                                    Text {
                                        id: contextValue
                                        text: parent.parent.modelData.value
                                        color: AppTheme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1220 ? 4 : 2
                columnSpacing: 12
                rowSpacing: 12

                Repeater {
                    model: [
                        { title: "TRAINING LOSS", value: AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--", chip: capture(trainingMessage, /step\s+(\d+)/i) ? "Step " + capture(trainingMessage, /step\s+(\d+)/i) : "Train stream", icon: "\u2198", iconColor: AppTheme.chartD },
                        { title: "VALIDATION LOSS", value: AppController.valLoss > 0 ? AppController.valLoss.toFixed(4) : "--", chip: capture(trainingMessage, /epoch\s+(\d+)/i) ? "Epoch " + capture(trainingMessage, /epoch\s+(\d+)/i) : "Validation stream", icon: "\u2198", iconColor: AppTheme.accentSecondary },
                        { title: "REASONING SCORE", value: AppController.accuracy > 0 ? (AppController.accuracy * 100).toFixed(1) + "%" : "--", chip: "Judge stream", icon: "\u2197", iconColor: AppTheme.success },
                        { title: "EVALUATION SCORE", value: page.reportMetric(), chip: evaluateMessage.length ? "Live eval" : "Latest report", icon: "\u25c8", iconColor: AppTheme.chartB }
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

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1220 ? 4 : 2
                columnSpacing: 12
                rowSpacing: 12

                GlassStatTile {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    label: "BEST TRAIN"
                    value: isNaN(page.bestOr(AppController.trainLossHistory, Math.min)) ? "--" : page.bestOr(AppController.trainLossHistory, Math.min).toFixed(4)
                    detail: "Lowest observed train loss in the active history."
                    accent: AppTheme.chartD
                    valueSize: 26
                }

                GlassStatTile {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    label: "BEST VALIDATION"
                    value: isNaN(page.bestOr(AppController.valLossHistory, Math.min)) ? "--" : page.bestOr(AppController.valLossHistory, Math.min).toFixed(4)
                    detail: "Lowest validation checkpoint captured so far."
                    accent: AppTheme.accentSecondary
                    valueSize: 26
                }

                GlassStatTile {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    label: "PEAK REASONING"
                    value: isNaN(page.bestOr(AppController.accuracyHistory, Math.max)) ? "--" : (page.bestOr(AppController.accuracyHistory, Math.max) * 100).toFixed(1) + "%"
                    detail: "Highest reasoning score across sampled evaluations."
                    accent: AppTheme.success
                    valueSize: 26
                }

                GlassStatTile {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    label: "LATEST REWARD"
                    value: page.formatFixed(page.lastOrNaN(AppController.rewardHistory), 3)
                    detail: "Most recent reward value emitted by the backend."
                    accent: AppTheme.chartC
                    valueSize: 26
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                SectionHeader {
                    Layout.fillWidth: true
                    title: "Curve Analysis"
                    subtitle: "Shift the time window to inspect drift, convergence, and live variance."
                }

                Rectangle {
                    height: 32
                    width: rangeRow.implicitWidth + 8
                    radius: 0
                    color: AppTheme.alpha(AppTheme.panelAlt, 0.36)
                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.07)
                    border.width: 1

                    Row {
                        id: rangeRow
                        anchors.centerIn: parent
                        spacing: 2

                        Repeater {
                            model: [
                                { label: "1m", points: 33 },
                                { label: "5m", points: 167 },
                                { label: "30m", points: 1000 },
                                { label: "1h", points: 2000 },
                                { label: "All", points: 0 }
                            ]

                            Rectangle {
                                required property var modelData
                                height: 24
                                width: rangeLabel.width + 16
                                radius: 0
                                color: page.chartRange === modelData.points ? AppTheme.alpha(AppTheme.accentPrimary, 0.10) : "transparent"
                                border.color: page.chartRange === modelData.points ? AppTheme.alpha(AppTheme.accentPrimary, 0.14) : "transparent"
                                border.width: 1

                                Text {
                                    id: rangeLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: page.chartRange === modelData.points ? AppTheme.textPrimary : AppTheme.textMuted
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: page.chartRange = modelData.points
                                }
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
                    Layout.preferredHeight: 326
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
                    Layout.preferredHeight: 326
                    title: "BEST WINDOWS"

                    Column {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: [
                                { label: "Best train loss", value: isNaN(page.bestOr(AppController.trainLossHistory, Math.min)) ? "--" : page.bestOr(AppController.trainLossHistory, Math.min).toFixed(4), accent: AppTheme.chartD },
                                { label: "Best validation loss", value: isNaN(page.bestOr(AppController.valLossHistory, Math.min)) ? "--" : page.bestOr(AppController.valLossHistory, Math.min).toFixed(4), accent: AppTheme.accentSecondary },
                                { label: "Peak reasoning score", value: isNaN(page.bestOr(AppController.accuracyHistory, Math.max)) ? "--" : (page.bestOr(AppController.accuracyHistory, Math.max) * 100).toFixed(1) + "%", accent: AppTheme.success },
                                { label: "Observed steps", value: AppController.trainLossHistory ? String(AppController.trainLossHistory.length) : "--", accent: AppTheme.accentPrimary }
                            ]

                            Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 48
                                radius: 8
                                color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.06)
                                border.color: Qt.rgba(modelData.accent.r, modelData.accent.g, modelData.accent.b, 0.14)
                                border.width: 1

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    color: modelData.accent
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8

                                    Text {
                                        id: bestLabel
                                        text: modelData.label
                                        color: AppTheme.textSecondary
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Item {
                                        width: Math.max(0, parent.width - bestLabel.width - bestValue.width - 8)
                                        height: 1
                                    }

                                    Text {
                                        id: bestValue
                                        text: modelData.value
                                        color: AppTheme.textPrimary
                                        font.pixelSize: 12
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
                    Layout.preferredHeight: 296
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
                    Layout.preferredHeight: 296
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
