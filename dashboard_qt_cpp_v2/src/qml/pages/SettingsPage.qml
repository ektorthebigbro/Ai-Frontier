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
    property bool saved: false

    readonly property var state: AppController.fullState || ({})
    readonly property var config: state["config"] || ({})
    readonly property var judgeConfig: config["large_judge"] || ({})
    readonly property var trainingConfig: config["training"] || ({})
    readonly property var curriculumConfig: config["curriculum"] || ({})
    readonly property var modelCatalog: state["model_catalog"] || ({})
    readonly property var judgePresets: modelCatalog["large_judge"] || []
    readonly property var modelCache: (state["model_cache"] || {})["large_judge"] || ({})

    function indexOfValue(listModel, value) {
        for (var i = 0; i < listModel.length; ++i) {
            if ((listModel[i] || {}).value === value) return i
        }
        return -1
    }

    function parseIntOr(text, fallback) {
        var value = parseInt(String(text || "").trim(), 10)
        return isNaN(value) ? fallback : value
    }

    function parseFloatOr(text, fallback) {
        var value = parseFloat(String(text || "").trim())
        return isNaN(value) ? fallback : value
    }

    function cacheSummary() {
        var items = []
        for (var key in modelCache) {
            if (modelCache[key]["cached"]) items.push(key + " (" + Number(modelCache[key]["size_mb"] || 0) + " MB)")
        }
        return items.length ? "Cached: " + items.join(", ") : "No models cached locally"
    }

    function cachedModelCount() {
        var count = 0
        for (var key in modelCache) {
            if (modelCache[key]["cached"])
                count += 1
        }
        return count
    }

    function activePrimaryLabel() {
        if (customModelId.text.trim().length)
            return customModelId.text.trim()
        if (primaryModelBox.currentText && primaryModelBox.currentText.length)
            return primaryModelBox.currentText.replace("  *", "")
        return "No model selected"
    }

    function activeFallbackLabel() {
        return fallbackModelBox.currentText && fallbackModelBox.currentText.length
            ? fallbackModelBox.currentText
            : "No fallback"
    }

    function refreshFormFromState() {
        var primaryIndex = indexOfValue(primaryModelBox.model || [], judgeConfig["model_id"] || "")
        if (primaryIndex >= 0 && primaryModelBox.currentIndex !== primaryIndex) primaryModelBox.currentIndex = primaryIndex

        var fallback = judgeConfig["fallback_model_ids"] && judgeConfig["fallback_model_ids"].length ? judgeConfig["fallback_model_ids"][0] : ""
        var fallbackIndex = indexOfValue(fallbackModelBox.model || [], fallback)
        if (fallbackIndex >= 0 && fallbackModelBox.currentIndex !== fallbackIndex) fallbackModelBox.currentIndex = fallbackIndex

        batchSizeBox.currentIndex = Math.max(0, ["1","2","4","8","16","32"].indexOf(String(trainingConfig["batch_size"] || 8)))
        autoDownloadBox.currentIndex = judgeConfig["auto_download_required_models"] === false ? 1 : 0
        cpuOffloadBox.currentIndex = trainingConfig["optimizer_cpu_offload"] === false ? 1 : 0
        mixedPrecisionBox.currentIndex = trainingConfig["mixed_precision"] === false ? 1 : 0
        curriculumStageBox.currentIndex = Math.max(0, Number(curriculumConfig["current_stage"] || 1) - 1)
        gradientCheckpointBox.currentIndex = trainingConfig["gradient_checkpointing"] === false ? 1 : 0

        if (!customModelId.activeFocus) customModelId.text = judgeConfig["model_id"] || ""
        if (!lrBox.activeFocus) lrBox.editText = String(trainingConfig["learning_rate"] || "0.0002")
        if (!vramCeilingField.activeFocus) vramCeilingField.text = String(trainingConfig["vram_ceiling_gb"] || "5.5")
        if (!microBatchField.activeFocus) microBatchField.text = String(trainingConfig["micro_batch_size"] || "2")
        if (!gradAccumField.activeFocus) gradAccumField.text = String(trainingConfig["gradient_accumulation_steps"] || "4")
        if (!warmupField.activeFocus) warmupField.text = String(trainingConfig["warmup_steps"] || "500")
        if (!maxGradNormField.activeFocus) maxGradNormField.text = String(trainingConfig["max_grad_norm"] || "1.0")
        if (!checkpointEveryField.activeFocus) checkpointEveryField.text = String(trainingConfig["checkpoint_every"] || "500")
        if (!evalEveryField.activeFocus) evalEveryField.text = String(trainingConfig["eval_every"] || "500")
        if (yamlEditor && !yamlEditor.activeFocus) yamlEditor.text = state["config_text"] || ""
    }

    function saveQuickConfig() {
        var cfg = {
            large_judge: {
                model_id: customModelId.text.trim().length ? customModelId.text.trim() : primaryModelBox.currentValue,
                fallback_model_ids: fallbackModelBox.currentValue ? [fallbackModelBox.currentValue] : [],
                auto_download_required_models: autoDownloadBox.currentIndex === 0,
                enabled: true
            },
            training: {
                batch_size: parseIntOr(batchSizeBox.currentText, 8),
                learning_rate: parseFloatOr(lrBox.editText || lrBox.currentText, 0.0002),
                vram_ceiling_gb: parseFloatOr(vramCeilingField.text, 5.5),
                optimizer_cpu_offload: cpuOffloadBox.currentIndex === 0,
                mixed_precision: mixedPrecisionBox.currentIndex === 0,
                micro_batch_size: parseIntOr(microBatchField.text, 2),
                gradient_accumulation_steps: parseIntOr(gradAccumField.text, 4),
                warmup_steps: parseIntOr(warmupField.text, 500),
                max_grad_norm: parseFloatOr(maxGradNormField.text, 1.0),
                checkpoint_every: parseIntOr(checkpointEveryField.text, 500),
                eval_every: parseIntOr(evalEveryField.text, 500),
                gradient_checkpointing: gradientCheckpointBox.currentIndex === 0
            },
            curriculum: {
                current_stage: curriculumStageBox.currentIndex + 1
            }
        }
        AppController.saveConfig(cfg)
        saved = true
        savedTimer.restart()
    }

    Timer {
        id: savedTimer
        interval: 2000
        onTriggered: page.saved = false
    }

    Connections {
        target: AppController
        function onStateChanged() { page.refreshFormFromState() }
    }

    // Helper component for styled label
    component FieldLabel: Text {
        property string labelText: ""
        text: labelText
        color: AppTheme.textMuted
        font.pixelSize: 10
        font.weight: Font.SemiBold
        font.letterSpacing: 0.4
    }

    // Helper component for styled ComboBox
    component StyledCombo: ComboBox {
        background: Rectangle { radius: 8; color: AppTheme.alpha(AppTheme.panelAlt, 0.56); border.color: parent.activeFocus ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08); border.width: 1 }
        contentItem: Text { leftPadding: 10; text: parent.displayText; color: AppTheme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    }

    // Helper for styled TextField
    component StyledField: TextField {
        background: Rectangle { radius: 8; color: AppTheme.alpha(AppTheme.panelAlt, 0.56); border.color: parent.activeFocus ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08); border.width: 1 }
        color: AppTheme.textSecondary
        font.pixelSize: 11
        padding: 9
    }

    Item {
        Component.onCompleted: Qt.callLater(page.refreshFormFromState)
        width: page.availableWidth
        implicitHeight: content.implicitHeight + 56

        ColumnLayout {
            id: content
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 20

            // Header row with save + advanced toggle
            RowLayout {
                Layout.fillWidth: true

                PageIntro {
                    Layout.fillWidth: true
                    title: "Configuration"
                    subtitle: "Model selection, training parameters, and curriculum settings."
                }

                ColumnLayout {
                    spacing: 10
                    anchors.bottom: parent.bottom

                    // Save button
                    Rectangle {
                        width: 150
                        height: 40
                        radius: 10
                        color: page.saved ? AppTheme.alpha(AppTheme.successStrong, 0.18)
                             : (saveHover.containsMouse ? AppTheme.alpha(AppTheme.accentPrimary, 0.18) : AppTheme.alpha(AppTheme.accentSecondary, 0.14))
                        border.color: page.saved ? AppTheme.alpha(AppTheme.success, 0.30) : AppTheme.alpha(AppTheme.accentPrimary, 0.26)
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 200 } }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                text: page.saved ? "OK" : "UP"
                                color: page.saved ? AppTheme.success : AppTheme.textPrimary
                                font.pixelSize: 12
                            }
                            Text {
                                text: page.saved ? "Saved" : "Save Settings"
                                color: page.saved ? AppTheme.success : AppTheme.textPrimary
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                        }

                        MouseArea {
                            id: saveHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: page.saveQuickConfig()
                        }
                    }

                    // Advanced toggle
                    Rectangle {
                        width: 150
                        height: 32
                        radius: 8
                        color: advHover.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.045)
                        border.color: page.advancedMode ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 100 } }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 6

                            Rectangle {
                                width: 14; height: 14; radius: 7
                                color: page.advancedMode ? AppTheme.alpha(AppTheme.accentPrimary, 0.18) : AppTheme.alpha(AppTheme.accentGlass, 0.05)
                                border.color: page.advancedMode ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 6; height: 6; radius: 3
                                    color: AppTheme.textPrimary
                                    visible: page.advancedMode
                                }
                            }

                            Text {
                                text: "Advanced"
                                color: page.advancedMode ? AppTheme.accentPrimary : AppTheme.textMuted
                                font.pixelSize: 11
                                Behavior on color { ColorAnimation { duration: 150 } }
                            }
                        }

                        MouseArea {
                            id: advHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: page.advancedMode = !page.advancedMode
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Repeater {
                    model: [
                        { label: "ACTIVE PRIMARY", value: activePrimaryLabel(), accent: AppTheme.accentPrimary },
                        { label: "TRAINING PROFILE", value: batchSizeBox.currentText + " batch | " + lrBox.editText, accent: AppTheme.accentSecondary },
                        { label: "MODEL CACHE", value: cachedModelCount() ? cachedModelCount() + " cached" : "Cold start", accent: AppTheme.success }
                    ]

                    GlassStatTile {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 86
                        label: modelData.label
                        value: modelData.value
                        accent: modelData.accent
                        valueSize: 14
                    }
                }
            }

            // ── Model + Training row ──────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                GlowCard {
                    Layout.fillWidth: true
                    implicitHeight: modelCol.implicitHeight + 50
                    title: "MODEL SELECTION"

                    Column {
                        id: modelCol
                        width: parent.width
                        spacing: 10

                        FieldLabel { labelText: "Primary Model" }
                        StyledCombo {
                            id: primaryModelBox
                            width: parent.width
                            textRole: "text"; valueRole: "value"
                            model: judgePresets.map(function(item) {
                                return { text: (item["label"] || item["id"] || "--") + (item["recommended"] ? "  *" : ""), value: item["id"] || "" }
                            })
                            Component.onCompleted: {
                                for (var i = 0; i < count; ++i) { if (valueAt(i) === judgeConfig["model_id"]) currentIndex = i }
                            }
                            function valueAt(i) { return model[i] ? model[i].value : "" }
                        }

                        FieldLabel { labelText: "Fallback Model" }
                        StyledCombo {
                            id: fallbackModelBox
                            width: parent.width
                            textRole: "text"; valueRole: "value"
                            model: judgePresets.map(function(item) { return { text: item["label"] || item["id"] || "--", value: item["id"] || "" } })
                            Component.onCompleted: {
                                var fb = judgeConfig["fallback_model_ids"] && judgeConfig["fallback_model_ids"].length ? judgeConfig["fallback_model_ids"][0] : ""
                                for (var i = 0; i < count; ++i) { if (valueAt(i) === fb) currentIndex = i }
                            }
                            function valueAt(i) { return model[i] ? model[i].value : "" }
                        }

                        FieldLabel { labelText: "Custom Model ID (overrides dropdown)" }
                        StyledField {
                            id: customModelId
                            width: parent.width
                            text: judgeConfig["model_id"] || ""
                            placeholderText: "e.g. Qwen/Qwen2.5-1.5B-Instruct"
                        }

                        FieldLabel { labelText: "Auto-Download Models" }
                        StyledCombo {
                            id: autoDownloadBox
                            width: parent.width
                            model: ["Enabled", "Disabled"]
                            currentIndex: judgeConfig["auto_download_required_models"] === false ? 1 : 0
                        }

                        Text {
                            text: cacheSummary()
                            color: AppTheme.textDim
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: trainCol.implicitHeight + 50
                        title: "TRAINING PARAMETERS"

                        ColumnLayout {
                            id: trainCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    FieldLabel { labelText: "Batch Size" }
                                    StyledCombo { id: batchSizeBox; Layout.fillWidth: true; model: ["1","2","4","8","16","32"]; currentIndex: Math.max(0, ["1","2","4","8","16","32"].indexOf(String(trainingConfig["batch_size"] || 8))) }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    FieldLabel { labelText: "Learning Rate" }
                                    StyledCombo {
                                        id: lrBox
                                        Layout.fillWidth: true
                                        editable: true
                                        model: ["1e-5","3e-5","1e-4","2e-4","3e-4","5e-4","1e-3"]
                                        Component.onCompleted: editText = String(trainingConfig["learning_rate"] || "0.0002")
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    FieldLabel { labelText: "VRAM Ceiling (GB)" }
                                    StyledField { id: vramCeilingField; Layout.fillWidth: true; text: String(trainingConfig["vram_ceiling_gb"] || "5.5"); validator: DoubleValidator { bottom: 0.0; top: 128.0 } }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    FieldLabel { labelText: "CPU Offload" }
                                    StyledCombo { id: cpuOffloadBox; Layout.fillWidth: true; model: ["Enabled", "Disabled"]; currentIndex: trainingConfig["optimizer_cpu_offload"] === false ? 1 : 0 }
                                }
                            }

                            FieldLabel { labelText: "Mixed Precision (FP16)" }
                            StyledCombo { id: mixedPrecisionBox; Layout.fillWidth: true; model: ["Enabled", "Disabled"]; currentIndex: trainingConfig["mixed_precision"] === false ? 1 : 0 }
                        }
                    }

                    GlowCard {
                        Layout.fillWidth: true
                        implicitHeight: currCol.implicitHeight + 50
                        title: "CURRICULUM"

                        Column {
                            id: currCol
                            width: parent.width
                            spacing: 10

                            FieldLabel { labelText: "Current Stage" }
                            StyledCombo {
                                id: curriculumStageBox
                                width: parent.width
                                model: [
                                    "Stage 1 - Basic Completion",
                                    "Stage 2 - Instruction Following",
                                    "Stage 3 - Reasoning",
                                    "Stage 4 - Complex Reasoning",
                                    "Stage 5 - Mastery"
                                ]
                                currentIndex: Math.max(0, Number(curriculumConfig["current_stage"] || 1) - 1)
                            }

                            Text {
                                text: "Each stage progressively increases task complexity and data mixing ratios."
                                color: AppTheme.textDim; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                GlowCard {
                    Layout.fillWidth: true
                    title: "CONFIG SNAPSHOT"
                    implicitHeight: snapshotColumn.implicitHeight + 48

                    Column {
                        id: snapshotColumn
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: [
                                { label: "Primary", value: activePrimaryLabel() },
                                { label: "Fallback", value: activeFallbackLabel() },
                                { label: "Precision", value: mixedPrecisionBox.currentText },
                                { label: "Curriculum", value: curriculumStageBox.currentText }
                            ]

                            Row {
                                required property var modelData
                                width: parent.width
                                height: 28

                                Text { text: modelData.label; color: AppTheme.accentSoft; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                Item { width: Math.max(0, parent.width - snapshotValue.width - 90); height: 1 }
                                Text {
                                    id: snapshotValue
                                    text: modelData.value
                                    color: AppTheme.textPrimary
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    width: Math.min(260, implicitWidth)
                                    horizontalAlignment: Text.AlignRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }

                GlowCard {
                    Layout.fillWidth: true
                    title: "SAVE BEHAVIOR"
                    implicitHeight: behaviorColumn.implicitHeight + 48

                    Column {
                        id: behaviorColumn
                        width: parent.width
                        spacing: 10

                        Text {
                            text: "Quick Save updates the structured config fields on this page. YAML Save writes the raw configuration directly when Advanced mode is open."
                            color: AppTheme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Rectangle {
                            width: parent.width
                            height: 54
                            radius: 12
                            color: AppTheme.alpha(AppTheme.panelAlt, 0.54)
                            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                            border.width: 1

                            Row {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                FlatTag {
                                    text: page.saved ? "Saved" : "Pending"
                                    tagColor: page.saved ? AppTheme.success : AppTheme.accentPrimary
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: page.saved ? "Your latest quick-config changes were sent to the backend." : "Make edits, then use Save Settings to persist them."
                                    color: AppTheme.textPrimary
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    width: parent.width - 110
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }

            // ── Advanced section (hidden by default) ─────────────────────
            GlowCard {
                visible: page.advancedMode
                Layout.fillWidth: true
                implicitHeight: internalsLayout.implicitHeight + 50
                title: "TRAINING INTERNALS"

                ColumnLayout {
                    id: internalsLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10

                    Text {
                        text: "Fine-tune throughput, memory behavior, and checkpoint cadence."
                        color: AppTheme.textDim; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 8

                        FieldLabel { labelText: "Micro Batch Size" }
                        FieldLabel { labelText: "Gradient Accumulation" }
                        StyledField { id: microBatchField; Layout.fillWidth: true; text: String(trainingConfig["micro_batch_size"] || "2"); validator: IntValidator { bottom: 1; top: 4096 } }
                        StyledField { id: gradAccumField; Layout.fillWidth: true; text: String(trainingConfig["gradient_accumulation_steps"] || "4"); validator: IntValidator { bottom: 1; top: 65536 } }

                        FieldLabel { labelText: "Warmup Steps" }
                        FieldLabel { labelText: "Max Grad Norm" }
                        StyledField { id: warmupField; Layout.fillWidth: true; text: String(trainingConfig["warmup_steps"] || "500"); validator: IntValidator { bottom: 0; top: 10000000 } }
                        StyledField { id: maxGradNormField; Layout.fillWidth: true; text: String(trainingConfig["max_grad_norm"] || "1.0"); validator: DoubleValidator { bottom: 0.0; top: 1000.0 } }

                        FieldLabel { labelText: "Checkpoint Every" }
                        FieldLabel { labelText: "Eval Every" }
                        StyledField { id: checkpointEveryField; Layout.fillWidth: true; text: String(trainingConfig["checkpoint_every"] || "500"); validator: IntValidator { bottom: 1; top: 10000000 } }
                        StyledField { id: evalEveryField; Layout.fillWidth: true; text: String(trainingConfig["eval_every"] || "500"); validator: IntValidator { bottom: 1; top: 10000000 } }
                    }

                    FieldLabel { labelText: "Gradient Checkpointing" }
                    StyledCombo { id: gradientCheckpointBox; Layout.fillWidth: true; model: ["Enabled", "Disabled"]; currentIndex: trainingConfig["gradient_checkpointing"] === false ? 1 : 0 }
                }
            }

            GlowCard {
                visible: page.advancedMode
                Layout.fillWidth: true
                implicitHeight: yamlLayout.implicitHeight + 50
                title: "FULL CONFIGURATION (YAML)"

                ColumnLayout {
                    id: yamlLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        height: 320
                        radius: 8
                        color: AppTheme.alpha(AppTheme.panelAlt, 0.48)
                        border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                        border.width: 1

                        ScrollView {
                            anchors.fill: parent
                            clip: true

                            TextArea {
                                id: yamlEditor
                                text: state["config_text"] || ""
                                color: AppTheme.textSecondary
                                background: null
                                font.family: "Courier New"
                                font.pixelSize: 12
                                wrapMode: TextArea.NoWrap
                                selectByMouse: true
                            }
                        }
                    }

                    GlassActionButton {
                        width: 120
                        height: 36
                        text: "Save YAML"
                        primary: true
                        onClicked: AppController.saveConfigYaml(yamlEditor.text)
                    }
                }
            }
        }
    }
}
