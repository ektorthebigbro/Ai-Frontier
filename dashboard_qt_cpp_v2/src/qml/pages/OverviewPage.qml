import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    background: null

    property bool advancedMode: false
    property int chartRange: 167

    readonly property var state: AppController.fullState || ({})
    readonly property var hardware: state["hardware"] || ({})
    readonly property var jobs: state["jobs"] || ({})
    readonly property var processes: state["processes"] || ({})
    readonly property var primaryJob: state["primary_job"] || ({})
    readonly property var config: state["config"] || ({})
    readonly property var judgeConfig: config["large_judge"] || ({})
    readonly property var alerts: state["alerts"] || []
    readonly property string reportText: state["report"] || ""
    readonly property string trainingMessage: jobs["training"] && jobs["training"]["message"] ? jobs["training"]["message"] : ""
    readonly property string primaryJobId: primaryJob["job"] || "autopilot"
    readonly property string primaryStage: primaryJob["stage"] || "idle"
    readonly property real primaryProgress: Number(primaryJob["progress"] || 0)
    readonly property string primaryEta: primaryJob["eta"] || "ETA unavailable"
    readonly property real primaryUpdatedAt: Number(primaryJob["updated_at"] || 0)
    readonly property var autopilotStages: [
        { id: "setup", title: "Environment", detail: "Repair Python, CUDA, and dependency health." },
        { id: "prepare", title: "Prepare Data", detail: "Refresh sources, tokenizers, and prepared datasets." },
        { id: "training", title: "Training", detail: "Resume the live run from the latest durable point." },
        { id: "evaluate", title: "Evaluation", detail: "Run the final score pass against the latest checkpoint." }
    ]

    function capture(text, regex) {
        var match = String(text || "").match(regex)
        return match && match.length > 1 ? match[1] : ""
    }

    function prettyJobName(job) {
        var value = String(job || "")
        if (value === "setup") return "Environment Setup"
        if (value === "prepare") return "Prepare Data"
        if (value === "training") return "Training"
        if (value === "evaluate") return "Evaluation"
        if (value === "inference") return "Inference"
        if (value === "autopilot") return "Autopilot"
        if (!value.length) return "Autopilot"
        return value.charAt(0).toUpperCase() + value.slice(1).replace(/_/g, " ")
    }

    function normalizeAutopilotStage(stage) {
        var value = String(stage || "").toLowerCase()
        if (value === "environment") return "setup"
        if (value === "dataset_prep") return "prepare"
        if (value === "evaluation") return "evaluate"
        return value
    }

    function progressPct(progress) {
        return (Math.max(0, Math.min(1, Number(progress || 0))) * 100).toFixed(1) + "%"
    }

    function relativeTime(ts) {
        var value = Number(ts || 0)
        if (!value) return "Updated recently"
        var delta = Math.max(0, Math.floor(Date.now() / 1000) - value)
        if (delta < 60) return "Updated just now"
        if (delta < 3600) return "Updated " + Math.floor(delta / 60) + "m ago"
        if (delta < 86400) return "Updated " + Math.floor(delta / 3600) + "h ago"
        return "Updated " + Math.floor(delta / 86400) + "d ago"
    }

    function stageColor(stage) {
        if (stage === "failed") return AppTheme.danger
        if (stage === "completed") return AppTheme.success
        if (stage === "paused") return AppTheme.warning
        if (stage === "idle" || stage === "stopped") return AppTheme.textMuted
        return AppTheme.accentSecondary
    }

    function recommendedStage() {
        var order = ["setup", "prepare", "training", "evaluate"]
        for (var i = 0; i < order.length; ++i) {
            if (normalizeAutopilotStage(String((jobs[order[i]] || {})["stage"] || "")) !== "completed")
                return order[i]
        }
        return "training"
    }

    function stageState(stageId) {
        var job = jobs[stageId] || ({})
        var stateValue = normalizeAutopilotStage(String(job["stage"] || "pending"))
        if (stateValue === "completed" || stateValue === "failed" || stateValue === "paused" || stateValue === "running")
            return stateValue
        return recommendedStage() === stageId ? "current" : "pending"
    }

    function stageProgress(stageId) {
        var job = jobs[stageId] || ({})
        return Number(job["progress"] || 0)
    }

    function stopActionPath() {
        if (primaryJobId === "training") return "/api/actions/train/stop"
        if (primaryJobId === "autopilot") return "/api/actions/autopilot/stop"
        if (primaryJobId === "inference") return "/api/actions/inference/stop"
        return ""
    }

    function sliceSeries(series) {
        if (!series || chartRange <= 0 || series.length <= chartRange) return series || []
        return series.slice(series.length - chartRange)
    }

    function evalSummary() {
        if (!reportText || !reportText.trim().length) return "--"
        try {
            var data = JSON.parse(reportText)
            if (data["protocol_overall_score"] !== undefined) return Number(data["protocol_overall_score"]).toFixed(2)
            if (data["reasoning_score"] !== undefined) return Number(data["reasoning_score"]).toFixed(2)
            if (data["gsm8k_accuracy"] !== undefined) return (Number(data["gsm8k_accuracy"]) * 100).toFixed(1) + "%"
        } catch (err) {}
        return "--"
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
                title: "AI Training Dashboard"
                subtitle: "The mission overview now prioritizes the active run, the curves that matter, and the exact stage sequence autopilot will follow."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1320 ? 5 : 1
                columnSpacing: 16
                rowSpacing: 16

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1320 ? 3 : 1
                    implicitHeight: missionColumn.implicitHeight + 42
                    title: "CURRENT MISSION"
                    badge: primaryStage
                    badgeColor: stageColor(primaryStage)

                    ColumnLayout {
                        id: missionColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 14

                        Text {
                            Layout.fillWidth: true
                            text: prettyJobName(primaryJobId)
                            color: AppTheme.textPrimary
                            font.pixelSize: 34
                            font.weight: Font.Bold
                            font.letterSpacing: -1.0
                        }

                        Text {
                            Layout.fillWidth: true
                            text: primaryStage === "idle" ? "Nothing is active right now. The deck stays ready for the next stage." : (primaryJob["message"] || prettyJobName(primaryJobId) + " is in progress.")
                            color: AppTheme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { label: "Progress", value: progressPct(primaryProgress), accent: AppTheme.accentPrimary },
                                    { label: "ETA", value: primaryEta, accent: AppTheme.chartC },
                                    { label: "Updated", value: relativeTime(primaryUpdatedAt), accent: AppTheme.chartB }
                                ]

                                GlassStatTile {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    height: 82
                                    label: modelData.label
                                    value: modelData.value
                                    accent: modelData.accent
                                    valueSize: 18
                                }
                            }
                        }

                        GradientProgressBar {
                            Layout.fillWidth: true
                            value: primaryProgress
                            label: "Workflow progress"
                            startColor: AppTheme.accentPrimary
                            endColor: AppTheme.chartC
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            GlassActionButton {
                                width: 180
                                height: 44
                                text: "Launch Smart Run"
                                primary: true
                                onClicked: AppController.postActionPath("/api/actions/autopilot/start")
                            }

                            GlassActionButton {
                                visible: stopActionPath().length > 0
                                width: 124
                                height: 44
                                text: "Stop"
                                danger: true
                                enabled: primaryStage !== "idle" && primaryStage !== "completed"
                                onClicked: AppController.postActionPath(stopActionPath())
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1320 ? 2 : 1
                    spacing: 16

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: quickColumn.implicitHeight + 42
                        title: "OPERATOR SNAPSHOT"

                        Column {
                            id: quickColumn
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: [
                                    { label: "Train step", value: capture(trainingMessage, /step\s+(\d+)/i) || "--" },
                                    { label: "Current loss", value: capture(trainingMessage, /loss=([\d.]+)/i) || (AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--") },
                                    { label: "Reward", value: capture(trainingMessage, /reward(?:_score)?=([\d.]+)/i) || "--" },
                                    { label: "Eval mean", value: evalSummary() }
                                ]

                                Row {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 8
                                    Text { text: modelData.label; color: AppTheme.textMuted; font.pixelSize: 11; width: 96 }
                                    Text { text: modelData.value; color: AppTheme.textPrimary; font.pixelSize: 12; font.weight: Font.DemiBold }
                                }
                            }
                        }
                    }

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: pulseColumn.implicitHeight + 42
                        title: "SYSTEM PULSE"

                        Column {
                            id: pulseColumn
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: [
                                    { label: "GPU load", value: Math.round(Number(hardware["gpu_utilization"] || 0)) + "%" },
                                    { label: "VRAM", value: (hardware["gpu_memory_used_mb"] || "--") + " / " + (hardware["gpu_memory_total_mb"] || "--") + " MB" },
                                    { label: "RAM", value: (Number(hardware["ram_used_mb"] || 0) / 1024).toFixed(1) + " / " + (Number(hardware["ram_total_mb"] || 0) / 1024).toFixed(1) + " GB" }
                                ]

                                Row {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 8
                                    Text { text: modelData.label; color: AppTheme.textMuted; font.pixelSize: 11; width: 74 }
                                    Text { text: modelData.value; color: AppTheme.textPrimary; font.pixelSize: 11; font.weight: Font.Medium; wrapMode: Text.WordWrap; width: parent.width - 82 }
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
                        { title: "TRAINING STATUS", value: primaryStage === "idle" ? "Idle" : prettyJobName(primaryJobId), chip: primaryStage, icon: "\u2665", iconColor: AppTheme.success },
                        { title: "CURRENT LOSS", value: capture(trainingMessage, /loss=([\d.]+)/i) || (AppController.trainLoss > 0 ? AppController.trainLoss.toFixed(4) : "--"), chip: capture(trainingMessage, /step\s+(\d+)/i) ? "Step " + capture(trainingMessage, /step\s+(\d+)/i) : "Training stream", icon: "\u2197", iconColor: AppTheme.chartD },
                        { title: "ACCURACY", value: evalSummary() !== "--" ? evalSummary() : "--", chip: "Latest report", icon: "\u26a1", iconColor: AppTheme.chartB },
                        { title: "TIME REMAINING", value: primaryStage === "idle" ? "--" : primaryEta, chip: "Estimated completion", icon: "\u23f1", iconColor: AppTheme.warning }
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
                    title: "Signal Deck"
                    subtitle: "Loss, reward, mission progress, and runtime load stay grouped in one scan path."
                }

                Rectangle {
                    radius: 10
                    height: 34
                    color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                    border.width: 1
                    width: segRow.implicitWidth + 8

                    Row {
                        id: segRow
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
                                readonly property bool active: page.chartRange === modelData.points
                                width: modelData.label === "30m" ? 40 : 36
                                height: 26
                                radius: 7
                                color: active ? AppTheme.alpha(AppTheme.accentPrimary, 0.16) : "transparent"
                                border.color: active ? AppTheme.alpha(AppTheme.accentPrimary, 0.24) : "transparent"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: parent.modelData.label
                                    color: parent.active ? AppTheme.textPrimary : AppTheme.textMuted
                                    font.pixelSize: 11
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
                    Layout.preferredHeight: 288
                    title: "TRAINING LOSS"
                    TrendChartWidget { width: parent.width; height: 216; series1: page.sliceSeries(AppController.trainLossHistory); label1: "Loss"; color1: AppTheme.chartD }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 288
                    title: "REWARD & PROGRESS"
                    TrendChartWidget { width: parent.width; height: 216; series1: page.sliceSeries(AppController.rewardHistory.length ? AppController.rewardHistory : AppController.progressHistory); series2: page.sliceSeries(AppController.rewardHistory.length ? AppController.progressHistory : []); label1: AppController.rewardHistory.length ? "Reward" : "Progress"; label2: AppController.rewardHistory.length ? "Progress" : ""; color1: AppTheme.chartC; color2: AppTheme.chartB }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 288
                    title: "RUNTIME LOAD"
                    TrendChartWidget { width: parent.width; height: 216; series1: page.sliceSeries(AppController.gpuUsageHistory); series2: page.sliceSeries(AppController.ramUsageHistory); label1: "GPU"; label2: "RAM"; color1: AppTheme.chartB; color2: AppTheme.chartA }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1280 ? 5 : 1
                columnSpacing: 16
                rowSpacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 3 : 1
                    spacing: 14

                    SectionHeader {
                        Layout.fillWidth: true
                        title: "Autopilot Timeline"
                        subtitle: "Completed and active blocks can be restarted directly from this command deck."
                    }

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: timelineColumn.implicitHeight + 42
                        title: "AUTOPILOT COMMAND DECK"

                        ColumnLayout {
                            id: timelineColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 14

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                GlassActionButton { width: 184; height: 42; text: "Launch Smart Run"; primary: true; onClicked: AppController.postActionPath("/api/actions/autopilot/start") }
                                GlassActionButton { width: 124; height: 42; text: "Pause Current"; muted: true; onClicked: AppController.postActionPath("/api/actions/autopilot/pause") }
                                GlassActionButton { width: 124; height: 42; text: "Resume Block"; muted: true; onClicked: AppController.postActionPath("/api/actions/autopilot/resume") }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: width > 1000 ? 4 : 2
                                columnSpacing: 12
                                rowSpacing: 12

                                Repeater {
                                    model: page.autopilotStages

                                    Rectangle {
                                        required property var modelData
                                        readonly property string cardState: stageState(modelData.id)
                                        readonly property real cardProgress: stageProgress(modelData.id)
                                        readonly property bool isActive: cardState === "current" || cardState === "running"
                                        readonly property bool isDone: cardState === "completed"
                                        readonly property color accentColor: isDone ? AppTheme.success : (cardState === "failed" ? AppTheme.danger : (cardState === "paused" ? AppTheme.warning : (isActive ? AppTheme.accentPrimary : AppTheme.textMuted)))

                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 184
                                        radius: 16
                                        color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, isDone || isActive ? 0.12 : 0.05)
                                        border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, isDone || isActive ? 0.24 : 0.10)
                                        border.width: 1

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 8

                                            Row {
                                                width: parent.width
                                                Text { text: "0" + (index + 1); color: AppTheme.textMuted; font.pixelSize: 11; font.weight: Font.Bold }
                                                Item { width: parent.width - stageChip.width - 20; height: 1 }
                                                PillBadge { id: stageChip; text: cardState; badgeColor: accentColor }
                                            }

                                            Text { text: modelData.title; color: AppTheme.textPrimary; font.pixelSize: 17; font.weight: Font.Bold }
                                            Text { text: modelData.detail; width: parent.width; color: AppTheme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                                            Text { text: progressPct(cardProgress); color: accentColor; font.pixelSize: 22; font.weight: Font.Bold }
                                            GradientProgressBar { width: parent.width; value: cardProgress; startColor: accentColor; endColor: isDone ? AppTheme.chartC : AppTheme.chartB }
                                        }

                                        MouseArea { anchors.fill: parent; onClicked: AppController.postActionPath("/api/actions/autopilot/continue/" + normalizeAutopilotStage(modelData.id)) }
                                    }
                                }
                            }
                        }
                    }

                    GlowCard {
                        visible: page.advancedMode
                        Layout.fillWidth: true
                        implicitHeight: judgeColumn.implicitHeight + 42
                        title: "ADVANCED CONTROLS"

                        Column {
                            id: judgeColumn
                            width: parent.width
                            spacing: 8
                            Text { width: parent.width; text: "Use the stronger local judge for quality scoring and reward shaping."; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Text { text: "Loaded model: " + (judgeConfig["model_id"] || "--"); color: AppTheme.textMuted; font.pixelSize: 11 }
                            GlassActionButton { width: 190; height: 36; text: judgeConfig["enabled"] ? "Disable Large Judge" : "Enable Large Judge"; danger: judgeConfig["enabled"]; primary: !judgeConfig["enabled"]; onClicked: AppController.postActionPath("/api/actions/large_judge/toggle") }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 2 : 1
                    spacing: 14

                    SectionHeader {
                        Layout.fillWidth: true
                        title: "System Rail"
                        subtitle: "Alerts and process state stay pinned here for quick scanning."
                    }

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: alertsColumn.implicitHeight + 42
                        title: "ALERTS"

                        Column {
                            id: alertsColumn
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: alerts.length ? alerts.slice(Math.max(0, alerts.length - 3)).reverse() : [{ message: "No recent alerts or warnings.", severity: "clear" }]
                                Rectangle {
                                    required property var modelData
                                    width: parent.width
                                    height: alertText.implicitHeight + 18
                                    radius: 12
                                    color: modelData.severity === "error" ? AppTheme.alpha(AppTheme.dangerStrong, 0.12) : (modelData.severity === "warning" ? AppTheme.alpha(AppTheme.warning, 0.10) : AppTheme.alpha(AppTheme.panelAlt, 0.46))
                                    border.color: modelData.severity === "error" ? AppTheme.alpha(AppTheme.danger, 0.22) : (modelData.severity === "warning" ? AppTheme.alpha(AppTheme.warning, 0.18) : AppTheme.alpha(AppTheme.accentGlass, 0.08))
                                    border.width: 1
                                    Text { id: alertText; anchors.fill: parent; anchors.margins: 12; text: modelData.message || String(modelData); color: AppTheme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: processColumn.implicitHeight + 42
                        title: "PROCESS SNAPSHOT"

                        Column {
                            id: processColumn
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: [
                                    { name: "Environment", key: "setup" },
                                    { name: "Prepare Data", key: "prepare" },
                                    { name: "Training", key: "training" },
                                    { name: "Evaluation", key: "evaluate" },
                                    { name: "Inference", key: "inference" },
                                    { name: "Autopilot", key: "autopilot" }
                                ]

                                Row {
                                    required property var modelData
                                    readonly property var jobData: jobs[modelData.key] || ({})
                                    readonly property var procData: processes[modelData.key] || ({})
                                    readonly property string procStage: procData["running"] ? "running" : (procData["paused"] ? "paused" : (String(jobData["stage"] || "idle")))
                                    readonly property color dotColor: procStage === "running" ? AppTheme.success : (procStage === "completed" ? AppTheme.chartC : (procStage === "failed" ? AppTheme.danger : AppTheme.textDim))
                                    width: parent.width
                                    spacing: 8
                                    Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: dotColor }
                                    Text { text: modelData.name; color: AppTheme.textMuted; font.pixelSize: 11; width: 92 }
                                    Text { text: procStage; color: dotColor; font.pixelSize: 11; font.weight: Font.DemiBold; width: 60 }
                                    Text { text: progressPct(jobData["progress"] || 0); color: AppTheme.textSecondary; font.pixelSize: 10 }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
