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
    readonly property var jobs: state["jobs"] || ({})
    readonly property var processes: state["processes"] || ({})
    readonly property var primaryJob: state["primary_job"] || ({})
    readonly property var alerts: state["alerts"] || []
    readonly property var recovery: state["recovery"] || ({})
    readonly property var jobOrder: ["setup", "prepare", "training", "evaluate", "inference", "autopilot"]

    function prettyJobName(job) {
        var value = String(job || "")
        if (value === "setup") return "Environment Setup"
        if (value === "prepare") return "Prepare Data"
        if (value === "training") return "Training"
        if (value === "evaluate") return "Evaluation"
        if (value === "inference") return "Inference"
        if (value === "autopilot") return "Autopilot"
        return value
    }

    function progressPct(progress) {
        return (Math.max(0, Math.min(1, Number(progress || 0))) * 100).toFixed(1) + "%"
    }

    function recommendedJob() {
        for (var i = 0; i < jobOrder.length; ++i) {
            if (String((jobs[jobOrder[i]] || {})["stage"] || "") !== "completed") return jobOrder[i]
        }
        return "training"
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

    function startEndpoint(name) {
        if (name === "training") return "/api/actions/train/start"
        if (name === "autopilot") return "/api/actions/autopilot/start"
        if (name === "inference") return "/api/actions/inference/start"
        return "/api/actions/" + name
    }

    function pauseEndpoint(name) {
        if (name === "training") return "/api/actions/train/pause"
        if (name === "autopilot") return "/api/actions/autopilot/pause"
        if (name === "inference") return "/api/actions/inference/pause"
        return "/api/actions/" + name + "/pause"
    }

    function resumeEndpoint(name) {
        if (name === "training") return "/api/actions/train/resume"
        if (name === "autopilot") return "/api/actions/autopilot/resume"
        if (name === "inference") return "/api/actions/inference/resume"
        return "/api/actions/" + name + "/resume"
    }

    function stopEndpoint(name) {
        if (name === "training") return "/api/actions/train/stop"
        if (name === "autopilot") return "/api/actions/autopilot/stop"
        if (name === "inference") return "/api/actions/inference/stop"
        return "/api/actions/" + name
    }

    function isPausedState(name) {
        if (name === "autopilot") {
            var autopilot = state["autopilot"] || ({})
            return Boolean(autopilot["paused"]) || Boolean(recovery["paused"])
        }
        return Boolean((processes[name] || {})["paused"]) || String((jobs[name] || {})["stage"] || "") === "paused"
    }

    function continueEndpoint(name) {
        if (name === "autopilot") return "/api/actions/autopilot/resume"
        if (name === "training") return isPausedState(name) ? "/api/actions/train/resume" : "/api/actions/train/start"
        if (name === "inference") return isPausedState(name) ? "/api/actions/inference/resume" : "/api/actions/inference/start"
        return isPausedState(name) ? "/api/actions/" + name + "/resume" : "/api/actions/" + name
    }

    function jobActionItems(name) {
        var items = []
        var running = Boolean((processes[name] || {})["running"])
        var paused = isPausedState(name)
        var supportsStop = name === "training" || name === "autopilot" || name === "inference"

        if (running) {
            items.push({ label: "Pause", path: pauseEndpoint(name), kind: "muted" })
        } else if (paused) {
            items.push({ label: "Resume", path: continueEndpoint(name), kind: "primary" })
        } else {
            items.push({ label: "Start", path: continueEndpoint(name), kind: "primary" })
        }

        if (supportsStop && (running || paused || Number((jobs[name] || {})["progress"] || 0) > 0))
            items.push({ label: "Stop", path: stopEndpoint(name), kind: "danger" })

        return items
    }

    function focusActionPath() {
        var recoveryJob = String(recovery["job"] || "")
        if (Boolean(recovery["can_pause"]) && recoveryJob.length)
            return recoveryJob === "autopilot" ? "/api/actions/autopilot/pause" : pauseEndpoint(recoveryJob)
        if (Boolean(recovery["can_resume"]) && recoveryJob.length)
            return continueEndpoint(recoveryJob)
        return startEndpoint(recommendedJob())
    }

    function focusActionLabel() {
        var recoveryJob = String(recovery["job"] || "")
        if (Boolean(recovery["can_pause"]) && recoveryJob.length)
            return recoveryJob === "autopilot" ? "Pause Current Block" : "Pause " + prettyJobName(recoveryJob)
        if (Boolean(recovery["can_resume"]) && recoveryJob.length)
            return (isPausedState(recoveryJob) ? "Resume " : "Continue ") + prettyJobName(recoveryJob)
        return "Run " + prettyJobName(recommendedJob())
    }

    function runningCount() {
        var count = 0
        for (var key in processes) if (processes[key]["running"]) count += 1
        return count
    }

    function resumableCount() {
        var count = 0
        for (var key in processes) if (processes[key]["paused"]) count += 1
        if (recovery["can_resume"] && !processes[String(recovery["job"] || "")]["paused"]) count += 1
        return count
    }

    function attentionCount() {
        var count = 0
        for (var key in jobs) if (String((jobs[key] || {})["stage"] || "") === "failed") count += 1
        return count + alerts.length
    }

    function missionName() {
        if (!String(primaryJob["job"] || "").length && Boolean(recovery["can_resume"]) && recovery["job"])
            return prettyJobName(recovery["job"]) + " Recovery Ready"
        return String(primaryJob["job"] || "").length ? prettyJobName(primaryJob["job"]) : "Pipeline Standby"
    }

    function missionStatusText() {
        if (Boolean(recovery["can_pause"])) return "live"
        if (Boolean(recovery["can_resume"])) return "resumable"
        if (!String(primaryJob["job"] || "").length) return "ready"
        return String(primaryJob["stage"] || "idle")
    }

    function missionSummary() {
        if (primaryJob["message"]) return primaryJob["message"]
        if (!String(primaryJob["job"] || "").length && Boolean(recovery["can_resume"]) && recovery["job"])
            return prettyJobName(recovery["job"]) + " can continue from the latest recovery point."
        if (!String(primaryJob["job"] || "").length)
            return prettyJobName(recommendedJob()) + " is next in sequence. Nothing is running right now."
        return prettyJobName(primaryJob["job"]) + " is waiting for the next operator decision."
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
                title: "Job Center"
                subtitle: "Treat this page as the operator cockpit: one place to launch, resume, pause, or inspect every pipeline block."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1280 ? 5 : 1
                columnSpacing: 16
                rowSpacing: 16

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 3 : 1
                    implicitHeight: missionColumn.implicitHeight + 42
                    title: "MISSION CONTROL"
                    badge: missionStatusText()
                    badgeColor: missionStatusText() === "failed" ? AppTheme.danger : (missionStatusText() === "resumable" ? AppTheme.warning : AppTheme.accentPrimary)

                    ColumnLayout {
                        id: missionColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 14

                        Text {
                            Layout.fillWidth: true
                            text: missionName()
                            color: AppTheme.textPrimary
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            font.letterSpacing: -0.8
                        }

                        Text {
                            Layout.fillWidth: true
                            text: missionSummary()
                            color: AppTheme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { label: "RUNNING", value: String(runningCount()), accent: AppTheme.success },
                                    { label: "RESUMABLE", value: String(resumableCount()), accent: AppTheme.warning },
                                    { label: "ATTENTION", value: String(attentionCount()), accent: AppTheme.danger }
                                ]

                                GlassStatTile {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    height: 84
                                    label: modelData.label
                                    value: modelData.value
                                    accent: modelData.accent
                                    valueSize: 24
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            GlassActionButton {
                                width: 220
                                height: 44
                                text: focusActionLabel()
                                primary: true
                                onClicked: AppController.postActionPath(focusActionPath())
                            }

                            Text {
                                Layout.fillWidth: true
                                text: progressPct(primaryJob["progress"] || 0) + " complete | " + relativeTime(primaryJob["updated_at"] || 0)
                                color: AppTheme.textMuted
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: content.width > 1280 ? 2 : 1
                    implicitHeight: alertColumn.implicitHeight + 42
                    title: "RECENT OPERATIONS"

                    Column {
                        id: alertColumn
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: alerts.length ? alerts.slice(Math.max(0, alerts.length - 5)).reverse() : [{ message: "No recent operational events. Alerts, recoveries, and backend actions will surface here.", severity: "clear" }]

                            Rectangle {
                                required property var modelData
                                readonly property bool isError: String(modelData.severity || "") === "error"
                                readonly property bool isWarn: String(modelData.severity || "") === "warning"
                                width: parent.width
                                height: operationText.implicitHeight + 20
                                radius: 12
                                color: isError ? AppTheme.alpha(AppTheme.dangerStrong, 0.12) : isWarn ? AppTheme.alpha(AppTheme.warning, 0.10) : AppTheme.alpha(AppTheme.panelAlt, 0.46)
                                border.color: isError ? AppTheme.alpha(AppTheme.danger, 0.22) : isWarn ? AppTheme.alpha(AppTheme.warning, 0.18) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Text {
                                    id: operationText
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    text: modelData.message || String(modelData)
                                    color: AppTheme.textSecondary
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            SectionHeader {
                Layout.fillWidth: true
                title: "Managed Workflows"
                subtitle: "Each workflow block carries its latest context, completion signal, and the fastest safe next action."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 1200 ? 2 : 1
                columnSpacing: 14
                rowSpacing: 14

                Repeater {
                    model: jobOrder

                    JobWorkflowCard {
                        required property string modelData
                        titleText: prettyJobName(modelData)
                        stageText: String((jobs[modelData] || {})["stage"] || "idle")
                        progressText: progressPct((jobs[modelData] || {})["progress"] || 0)
                        messageText: (jobs[modelData] || {})["message"] || "No recent context."
                        progressValue: Number((jobs[modelData] || {})["progress"] || 0)
                        metaText: "ETA: " + String((jobs[modelData] || {})["eta"] || "unknown") + " | Updated " + relativeTime((jobs[modelData] || {})["updated_at"] || 0)
                        running: Boolean((processes[modelData] || {})["running"])
                        paused: Boolean((processes[modelData] || {})["paused"]) || String((jobs[modelData] || {})["stage"] || "") === "paused"
                        actionItems: jobActionItems(modelData)
                        onActionTriggered: function(path) { AppController.postActionPath(path) }
                    }
                }
            }
        }
    }
}
