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
    readonly property var config: state["config"] || ({})
    readonly property var datasets: config["datasets"] || ({})
    readonly property var history: state["history"] || ({})
    readonly property var modelCache: (state["model_cache"] || {})["large_judge"] || ({})
    readonly property var fileHistory: history["files"] || []
    readonly property var actionHistory: history["actions"] || []

    function cachedModels() {
        var items = []
        for (var key in modelCache) {
            var row = modelCache[key]
            items.push({
                id: key,
                cached: Boolean(row["cached"]),
                size_mb: Number(row["size_mb"] || 0),
                has_model: Boolean(row["has_model"]),
                has_tokenizer: Boolean(row["has_tokenizer"])
            })
        }
        return items
    }

    function totalCacheMb() {
        var total = 0
        var count = 0
        for (var key in modelCache) {
            if (modelCache[key]["cached"]) {
                total += Number(modelCache[key]["size_mb"] || 0)
                count += 1
            }
        }
        return count ? count + " models (" + total + " MB)" : "No models cached"
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
                title: "Data & Dependencies"
                subtitle: "Manage datasets, dependency cache, and the live artifact trail with clearer sectioned controls."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: [
                        { label: "TOTAL SAMPLES", value: datasets["max_samples"] ? Math.max(1, Math.round(Number(datasets["max_samples"]) / 1000)) + "k" : "--", accent: AppTheme.accentPrimary },
                        { label: "CACHE STATUS",  value: totalCacheMb(),                accent: AppTheme.success },
                        { label: "DEPENDENCIES",  value: cachedModels().length + " cached", accent: AppTheme.chartB }
                    ]
                    GlassStatTile {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 92
                        label: modelData.label
                        value: modelData.value
                        accent: modelData.accent
                        valueSize: 22
                    }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                title: "DATASETS"
                implicitHeight: datasetColumn.implicitHeight + frameHeight

                ColumnLayout {
                    id: datasetColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10

                    Text { text: "Configured sources and rebuild actions for the preparation pipeline."; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                    Flow {
                        Layout.fillWidth: true
                        width: parent.width
                        spacing: 10
                        Repeater {
                            model: [
                                { label: "Rebuild Dataset", path: "/api/actions/prepare", primary: false },
                                { label: "Check Integrity", path: "/api/actions/data/check", primary: false },
                                { label: "Re-download All", path: "/api/actions/data/redownload", primary: true },
                                { label: "Delete Prepared Data", path: "/api/actions/data/cleanup/data", danger: true },
                                { label: "Delete Source Cache", path: "/api/actions/data/cleanup/dataset_cache", danger: true }
                            ]
                            GlassActionButton {
                                required property var modelData
                                width: Math.max(120, text.length * 7 + 30)
                                text: modelData.label
                                primary: modelData.primary
                                danger: modelData.danger
                                onClicked: AppController.postActionPath(modelData.path)
                            }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: datasets["sources"] || []
                            Rectangle {
                                required property var modelData
                                width: datasetColumn.width
                                height: sourceDesc.implicitHeight + 54
                                radius: 12
                                color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                                border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6
                                    Text { text: modelData["name"] || "Dataset"; color: AppTheme.textPrimary; font.pixelSize: 14; font.weight: Font.DemiBold }
                                    Text { text: "Type: " + (modelData["type"] || "--") + " | Weight: " + Number(modelData["weight"] || 1).toFixed(2); color: AppTheme.textMuted; font.pixelSize: 11 }
                                    Text { id: sourceDesc; text: (modelData["path"] || modelData["url"] || "Configured source"); color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                                }
                            }
                        }

                        Rectangle {
                            visible: !(datasets["sources"] || []).length
                            width: datasetColumn.width
                            height: 92
                            radius: 12
                            color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 6
                                Text { text: "No dataset sources configured"; color: AppTheme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
                                Text { text: "Add sources in configs/default.yaml to unlock rebuild, integrity, and lineage actions here."; color: AppTheme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                            }
                        }
                    }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                title: "CACHED MODELS"
                implicitHeight: cacheColumn.implicitHeight + frameHeight

                ColumnLayout {
                    id: cacheColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10

                    Text { text: "Installed dependency cache, judge models, and environment recovery actions."; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                    Flow {
                        Layout.fillWidth: true
                        width: parent.width
                        spacing: 10
                        Repeater {
                            model: [
                                { label: "Clear All Cached Models", path: "/api/actions/data/clear_cache", danger: true },
                                { label: "Delete Environment", path: "/api/actions/data/cleanup/dependencies", danger: true },
                                { label: "Refresh Environment", path: "/api/actions/setup" }
                            ]
                            GlassActionButton {
                                required property var modelData
                                width: Math.max(140, text.length * 7 + 28)
                                text: modelData.label
                                danger: modelData.danger
                                muted: !modelData.danger
                                onClicked: AppController.postActionPath(modelData.path)
                            }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            visible: !cachedModels().length
                            width: cacheColumn.width
                            height: 88
                            radius: 12
                            color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 6
                                Text { text: "No cached models yet"; color: AppTheme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
                                Text { text: "This area will fill in as model downloads and dependency setup complete."; color: AppTheme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                            }
                        }

                        Repeater {
                            model: cachedModels()
                            Rectangle {
                                required property var modelData
                                width: cacheColumn.width
                                height: 90
                                radius: 12
                                color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                                border.color: modelData.cached ? AppTheme.alpha(AppTheme.success, 0.34) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 12

                                    Column {
                                        width: parent.width - 110
                                        spacing: 6
                                        Text { text: modelData.id; color: AppTheme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.WrapAnywhere; width: parent.width }
                                        Text { text: (modelData.cached ? "Ready" : "Incomplete") + " | " + modelData.size_mb + " MB"; color: modelData.cached ? AppTheme.success : AppTheme.textDim; font.pixelSize: 11 }
                                        Text { text: (modelData.has_model ? "Weights ok" : "Weights missing") + " | " + (modelData.has_tokenizer ? "Tokenizer ok" : "Tokenizer missing"); color: AppTheme.textMuted; font.pixelSize: 11 }
                                    }

                                    GlassActionButton {
                                        width: 80
                                        text: "Delete"
                                        danger: true
                                        anchors.verticalCenter: parent.verticalCenter
                                        onClicked: AppController.postActionPath("/api/actions/data/remove_model/" + encodeURIComponent(modelData.id))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Row {
                spacing: 10
                Rectangle { width: 3; height: 38; radius: 1.5; color: AppTheme.accentSecondary; anchors.verticalCenter: parent.verticalCenter }
                Column {
                    spacing: 3
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: "Lineage & Audit"; color: AppTheme.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold; font.letterSpacing: -0.3 }
                    Text { text: "Artifacts and workflow actions grouped as a readable project trail."; color: AppTheme.textSecondary; font.pixelSize: 11 }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                title: "ARTIFACT LINEAGE"
                implicitHeight: artifactColumn.implicitHeight + frameHeight

                ColumnLayout {
                    id: artifactColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10
                    Text { text: "Tracking generated outputs and runtime logs that still exist on disk."; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                    GlassActionButton { width: 126; text: "Delete All Logs"; danger: true; onClicked: AppController.postActionPath("/api/actions/data/cleanup/logs") }

                    Column {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: fileHistory
                            Rectangle {
                                required property var modelData
                                width: artifactColumn.width
                                height: artifactDesc.implicitHeight + 76
                                radius: 12
                                color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                                border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6
                                    Text { text: modelData["label"] || "Artifact"; color: AppTheme.textPrimary; font.pixelSize: 14; font.weight: Font.DemiBold }
                                    Text { text: (modelData["category_label"] || "--") + " | " + (modelData["stage_label"] || "--"); color: AppTheme.textMuted; font.pixelSize: 11 }
                                    Text { id: artifactDesc; text: modelData["relative_path"] || ""; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                                    Row {
                                        spacing: 8
                                        GlassActionButton { width: 70; text: "Open"; muted: true; onClicked: AppController.openLocalPath(modelData["path"] || "") }
                                        GlassActionButton { width: 76; text: "Delete"; danger: true; onClicked: AppController.postActionPath("/api/actions/data/delete_path/" + encodeURIComponent(modelData["relative_path"] || "")) }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                title: "ACTION TRAIL"
                implicitHeight: trailColumn.implicitHeight + frameHeight

                ColumnLayout {
                    id: trailColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10
                    Text { text: "Recent durable actions with their live file links."; color: AppTheme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                    GlassActionButton { width: 140; text: "Clear Action Trail"; danger: true; onClicked: AppController.postActionPath("/api/actions/data/clear_action_history") }

                    Column {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: actionHistory
                            Rectangle {
                                required property var modelData
                                width: trailColumn.width
                                height: actionDesc.implicitHeight + 72
                                radius: 12
                                color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                                border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6
                                    Text { text: modelData["message"] || "Action"; color: AppTheme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; width: parent.width }
                                    Text { text: (modelData["category_label"] || "--") + " / " + (modelData["action_label"] || "--") + " | " + (modelData["severity"] || "info"); color: AppTheme.textMuted; font.pixelSize: 11 }
                                    Text { id: actionDesc; text: (modelData["paths"] || []).join(" | "); color: AppTheme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                                    GlassActionButton { width: 76; text: "Delete"; danger: true; onClicked: AppController.postActionPath("/api/actions/data/delete_action/" + encodeURIComponent(modelData["signature"] || "")) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
