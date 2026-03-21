import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    background: null

    readonly property var diag: AppController.diagnostics || ({})
    readonly property var healthRows: diag["health"] || []
    readonly property var issues: diag["issues"] || []
    readonly property var fixes: diag["fixes_applied"] || []
    readonly property var logs: diag["logs"] || []
    readonly property var reloadableModules: diag["reloadable_modules"] || []
    readonly property var logSummary: diag["log_summary"] || ({})
    readonly property string fetchError: diag["fetch_error"] || ""

    property var selectedIssue: ({})
    property string issueDetailText: ""

    function timestamp(ts) {
        if (!ts) return "unknown"
        return Qt.formatDateTime(new Date(Number(ts) * 1000), "yyyy-MM-dd HH:mm:ss")
    }

    function prettyModuleName(module) {
        if (module === "frontier.hardware")              return "Hardware Telemetry"
        if (module === "frontier.config")                return "Config Loader"
        if (module === "frontier.utils")                 return "Shared Utilities"
        if (module === "frontier.model_management")      return "Model Catalog"
        if (module === "frontier.modeling")              return "Generator Model Code"
        if (module === "frontier.data")                  return "Training Data Helpers"
        if (module === "dataset_pipeline.build_dataset") return "Prepare Pipeline"
        if (module === "frontier.judging")               return "Judge Protocols"
        if (!String(module || "").length)                return "Runtime"
        return String(module)
    }

    function reloadGuide(module) {
        if (module === "frontier.hardware")              return "Refreshes GPU, VRAM, RAM, and hardware probe state without restarting managed workers."
        if (module === "frontier.config")                return "Reloads configs/default.yaml from disk and refreshes runtime state that depends on config values."
        if (module === "frontier.utils")                 return "Clears shared runtime caches so the next worker launch uses updated utility helpers."
        if (module === "frontier.model_management")      return "Refreshes model catalog visibility, cache summaries, and download state in the dashboard."
        if (module === "frontier.modeling")              return "Applies generator model code changes. If inference is running it will restart; otherwise changes apply on the next run."
        if (module === "frontier.data")                  return "Applies shared scoring and dataset helper changes."
        if (module === "dataset_pipeline.build_dataset") return "Applies prepare-entrypoint changes like source loading, tokenizer training, and dataset writing."
        if (module === "frontier.judging")               return "Applies large-judge parsing and protocol logic for evaluation and inference."
        return "No helper text is available for this module yet."
    }

    function reloadTargetForIssueModule(module) {
        if (module === "prepare") return "dataset_pipeline.build_dataset"
        return module
    }

    function selectedModule() {
        if (moduleBox.currentIndex < 0 || moduleBox.currentIndex >= moduleBox.model.length) return ""
        return moduleBox.model[moduleBox.currentIndex].value
    }

    function logColor(entry) {
        var level = String((entry["level"] || entry["severity"] || "")).toLowerCase()
        if (level === "error")   return AppTheme.danger
        if (level === "warning") return AppTheme.warning
        if (level === "debug")   return AppTheme.textDim
        return AppTheme.textSecondary
    }

    function fixSeverityColor(fix) {
        var s = String(fix["severity"] || fix["level"] || "info").toLowerCase()
        if (s === "error")   return AppTheme.danger
        if (s === "warning") return AppTheme.warning
        return AppTheme.success
    }

    Connections {
        target: AppController
        function onDiagnosticIssueLoaded(issue, detailText, success) {
            if (issue && issue["key"] && page.selectedIssue["key"] && issue["key"] !== page.selectedIssue["key"]) return
            if (issue && issue["key"]) page.selectedIssue = issue
            page.issueDetailText = detailText || (success ? "No issue report returned." : "Issue deep-dive failed.")
        }
    }

    Item {
        width: page.availableWidth
        implicitHeight: content.implicitHeight + 56

        ColumnLayout {
            id: content
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 18

            PageIntro {
                Layout.fillWidth: true
                title: "Diagnostics"
                subtitle: "Run health checks, inspect active runtime issues, and hot-reload safe modules without restarting the server."
            }

            // Fetch error banner
            GlowCard {
                visible: !!page.fetchError
                Layout.fillWidth: true
                implicitHeight: errorTxt.implicitHeight + 22
                title: "DIAGNOSTICS NOTICE"

                Text {
                    id: errorTxt
                    anchors.fill: parent; anchors.margins: 11; anchors.leftMargin: 14
                    text: "Diagnostics refresh error: " + page.fetchError
                    color: AppTheme.danger; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
            }

            // Log summary stats
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        { label: "ERRORS",   value: Number(logSummary["error_count"]   || 0), accent: AppTheme.danger },
                        { label: "WARNINGS", value: Number(logSummary["warning_count"] || 0), accent: AppTheme.warning },
                        { label: "INFO",     value: Number(logSummary["info_count"]    || 0), accent: AppTheme.accentPrimary },
                        { label: "ISSUES",   value: issues.length,                             accent: issues.length ? AppTheme.danger : AppTheme.success }
                    ]
                    GlassStatTile {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 78
                        label: modelData.label
                        value: String(modelData.value)
                        accent: modelData.accent
                        valueSize: 26
                    }
                }
            }

            // Health + Repair row
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                GlowCard {
                    Layout.fillWidth: true
                    implicitHeight: Math.max(280, healthList.implicitHeight + 56)
                    title: "HEALTH CHECKS"

                    Column {
                        id: healthList
                        width: parent.width
                        spacing: 6

                        Repeater {
                            model: healthRows.length ? healthRows : [{ check: "No health data returned.", status: "info", message: "" }]
                            DiagnosticsHealthRow {
                                required property var modelData
                                width: parent.width
                                checkName: modelData.check || "Check"
                                messageText: modelData.message || ""
                                status: modelData.status || "info"
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.preferredWidth: 360
                    implicitHeight: repairColumn.implicitHeight + 56
                    title: "REPAIR CONTROLS"

                    Column {
                        id: repairColumn
                        width: parent.width
                        spacing: 10

                        Text { text: "Reloadable Module"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.SemiBold; font.letterSpacing: 0.4 }
                        ComboBox {
                            id: moduleBox
                            width: parent.width
                            model: reloadableModules.map(function(name) { return { label: prettyModuleName(name), value: name } })
                            textRole: "label"
                            background: Rectangle { radius: 8; color: AppTheme.alpha(AppTheme.panelAlt, 0.54); border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08); border.width: 1 }
                            contentItem: Text { leftPadding: 10; text: moduleBox.displayText; color: AppTheme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                        }
                        Text {
                            text: selectedModule().length ? reloadGuide(selectedModule()) : "Select a module to see reload guidance."
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Flow {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: [
                                    { label: "Launch Backend", primary: true,  fn: function() { AppController.launchBackend() } },
                                    { label: "Run Checks",     primary: false, fn: function() { AppController.runDiagnosticChecks() } },
                                    { label: "Hot Reload",     primary: false, fn: function() { if (selectedModule().length) AppController.reloadModule(selectedModule()) } },
                                    { label: "Self-Heal",      primary: false, fn: function() { AppController.selfHeal() } }
                                ]
                                GlassActionButton {
                                    required property var modelData
                                    width: Math.max(94, text.length * 7 + 26)
                                    text: modelData.label
                                    primary: modelData.primary
                                    muted: !modelData.primary
                                    onClicked: modelData.fn()
                                }
                            }
                        }

                        Row {
                            spacing: 8
                            GlassActionButton { text: "Clear Cache"; danger: true; onClicked: AppController.postActionPath("/api/diagnostics/cache/clear") }
                            GlassActionButton { text: "Clear All Issues"; danger: true; onClicked: AppController.clearDiagnostics() }
                        }
                    }
                }
            }

            // Active Issues + sidebar row
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                GlowCard {
                    Layout.fillWidth: true
                    implicitHeight: Math.max(200, issuesList.implicitHeight + 56)
                    title: "ACTIVE ISSUES"

                    Column {
                        id: issuesList
                        width: parent.width
                        spacing: 8

                        RowLayout {
                            width: parent.width
                            Text { text: "Issues requiring attention"; color: AppTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true }
                            FlatTag {
                                text: issues.length ? String(issues.length) + " issues" : "clear"
                                tagColor: issues.length ? AppTheme.danger : AppTheme.success
                            }
                        }

                        Repeater {
                            model: issues.length ? issues : [{ error: "No active runtime issues", severity: "clear", key: "" }]
                            DiagnosticsIssueCard {
                                required property var modelData
                                issueData: modelData
                                moduleText: prettyModuleName(modelData.module || "")
                                severityText: modelData.severity || "error"
                                seenText: timestamp(modelData.last_seen || modelData.last_seen_ts || modelData.ts || 0)
                                issueText: modelData.error || modelData.message || "No active runtime issues"
                                issueMetaText: (!!modelData.count || !!modelData.suppressed)
                                    ? ("Count: " + Number(modelData.count || 1) + (modelData.suppressed ? " | suppressed after repeated failures" : ""))
                                    : ""
                                onClearRequested: function(key) { AppController.clearDiagnosticKey(key) }
                                onReloadRequested: function(moduleName) { AppController.reloadModule(reloadTargetForIssueModule(moduleName)) }
                                onDiveRequested: function(issue) {
                                    page.selectedIssue = issue
                                    page.issueDetailText = "Loading issue diagnostics..."
                                    issueDrawer.open()
                                    if (issue.key) AppController.fetchDiagnosticIssue(issue.key)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 400
                    spacing: 14

                    // Fix History
                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: fixHistoryColumn.implicitHeight + 56
                        title: "FIX HISTORY"

                        Column {
                            id: fixHistoryColumn
                            width: parent.width
                            spacing: 6

                            Text {
                                visible: !fixes.length
                                text: "No fixes applied yet."
                                color: AppTheme.textMuted; font.pixelSize: 11
                            }

                            Repeater {
                                model: fixes.slice(0, 6)
                                Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: parent.width
                                    implicitHeight: fixMsg.implicitHeight + 16
                                    radius: 8
                                    color: AppTheme.alpha(AppTheme.panelAlt, 0.48)
                                    border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                    border.width: 1

                                    Rectangle {
                                        width: 3; radius: 1.5
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                        anchors.margins: 2
                                        color: fixSeverityColor(modelData)
                                        opacity: 0.8
                                    }

                                    Column {
                                        anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 12
                                        spacing: 2
                                        Text {
                                            id: fixMsg
                                            text: modelData["message"] || modelData["action"] || JSON.stringify(modelData)
                                            color: AppTheme.textSecondary; font.pixelSize: 11
                                            wrapMode: Text.WordWrap; width: parent.width
                                        }
                                        Text {
                                            visible: !!(modelData["module"] || modelData["ts"])
                                            text: (modelData["module"] ? modelData["module"] + " | " : "") + (modelData["ts"] ? Qt.formatDateTime(new Date(Number(modelData["ts"]) * 1000), "HH:mm:ss") : "")
                                            color: AppTheme.textMuted; font.pixelSize: 9
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Backend Log
                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: backendLogColumn.implicitHeight + 56
                        title: "BACKEND LOG"

                        Column {
                            id: backendLogColumn
                            width: parent.width
                            spacing: 2

                            Text {
                                visible: !logs.length
                                text: "No backend log loaded."
                                color: AppTheme.textMuted; font.pixelSize: 11
                            }

                            Repeater {
                                model: logs.slice(Math.max(0, logs.length - 10))
                                Item {
                                    required property var modelData
                                    width: parent.width
                                    height: 22

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 8

                                        Rectangle {
                                            width: 6; height: 6; radius: 3
                                            color: logColor(modelData)
                                            Layout.alignment: Qt.AlignVCenter
                                        }

                                        Text {
                                            text: modelData["ts"]
                                                ? Qt.formatDateTime(new Date(Number(modelData["ts"]) * 1000), "HH:mm:ss")
                                                : "--:--:--"
                                            color: AppTheme.textDim; font.pixelSize: 10; font.family: "Courier New"
                                            Layout.preferredWidth: 56
                                        }

                                        Text {
                                            text: modelData["message"] || JSON.stringify(modelData)
                                            color: logColor(modelData)
                                            font.pixelSize: 10; font.family: "Courier New"
                                            Layout.fillWidth: true; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Drawer {
        id: issueDrawer
        edge: Qt.RightEdge
        width: Math.min(420, page.width * 0.38)
        height: page.height
        modal: true

        Rectangle {
            anchors.fill: parent
            color: AppTheme.alpha(AppTheme.chromeElevated, 0.92)
            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.10)

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Text { text: "Issue Deep Dive"; color: AppTheme.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold; font.letterSpacing: -0.3 }
                Text {
                    text: page.selectedIssue["error"] || page.selectedIssue["message"] || "No issue selected"
                    color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width
                }

                Rectangle { width: parent.width; height: 1; color: AppTheme.alpha(AppTheme.accentGlass, 0.08) }

                ScrollView {
                    width: parent.width
                    height: parent.height - 152
                    clip: true

                    TextArea {
                        readOnly: true
                        text: page.issueDetailText.length ? page.issueDetailText : JSON.stringify(page.selectedIssue, null, 2)
                        color: AppTheme.textSecondary
                        background: null
                        font.family: "Courier New"
                        font.pixelSize: 11
                        wrapMode: TextArea.WrapAnywhere
                        selectByMouse: true
                    }
                }

                Row {
                    spacing: 8

                    GlassActionButton {
                        width: 112
                        text: "Copy Report"
                        primary: true
                        onClicked: AppController.copyText(
                            page.issueDetailText.length ? page.issueDetailText : JSON.stringify(page.selectedIssue, null, 2),
                            "Issue report copied")
                    }

                    GlassActionButton {
                        width: 84
                        text: "Close"
                        muted: true
                        onClicked: issueDrawer.close()
                    }
                }
            }
        }
    }
}
