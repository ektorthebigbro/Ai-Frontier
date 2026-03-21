import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiFrontier.Backend

Item {
    id: page

    property var messages: []
    property bool sending: false
    readonly property var state: AppController.fullState || ({})
    readonly property var checkpoint: state["checkpoint"] || ({})
    readonly property var jobs: state["jobs"] || ({})
    readonly property bool canSendMessage: AppController.connected && !sending && chatInput.text.trim().length > 0

    function addMessage(role, text) {
        var copy = messages.slice()
        copy.push({ role: role, text: text })
        messages = copy
        messageList.model = messages
    }

    function appendPreset(prompt) {
        if (chatInput.text.length && !/\s$/.test(chatInput.text))
            chatInput.text += " "
        chatInput.text += prompt
        chatInput.forceActiveFocus()
    }

    function numericFieldValue(field, fallback) {
        var value = Number(field.text)
        return isNaN(value) ? fallback : value
    }

    function sendMessage() {
        var msg = chatInput.text.trim()
        if (!canSendMessage)
            return

        addMessage("user", msg)
        chatInput.text = ""
        sending = true

        AppController.sendChatMessage(msg, {
            temperature: numericFieldValue(tempField, 0.7),
            max_new_tokens: numericFieldValue(maxTokensField, 256),
            top_k: numericFieldValue(topKField, 50),
            top_p: numericFieldValue(topPField, 0.9),
            checkpoint_path: checkpointBox.currentValue
        })
    }

    Connections {
        target: AppController
        function onChatResponseReceived(response) {
            page.sending = false
            page.addMessage("assistant", response)
        }
    }

    component StyledField: TextField {
        color: AppTheme.textPrimary
        font.pixelSize: 11
        padding: 9
        background: Rectangle {
            radius: 10
            color: AppTheme.alpha(AppTheme.panelAlt, 0.56)
            border.color: parent.activeFocus ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
            border.width: 1
        }
    }

    component StyledCombo: ComboBox {
        background: Rectangle {
            radius: 10
            color: AppTheme.alpha(AppTheme.panelAlt, 0.56)
            border.color: parent.activeFocus ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
            border.width: 1
        }
        contentItem: Text {
            leftPadding: 10
            text: parent.displayText
            color: AppTheme.textSecondary
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        PageIntro {
            Layout.fillWidth: true
            title: "Inference Chat"
            subtitle: "Interrogate the latest checkpoint with reusable prompts, live generation controls, and a cleaner split between dialogue and tuning."
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 1320 ? 5 : 1
            columnSpacing: 16
            rowSpacing: 16

            GlowCard {
                Layout.fillWidth: true
                Layout.columnSpan: page.width > 1320 ? 3 : 1
                implicitHeight: statusColumn.implicitHeight + frameHeight
                title: "CHAT MISSION"
                badge: checkpoint["available"] ? "checkpoint ready" : "no checkpoint"
                badgeColor: checkpoint["available"] ? AppTheme.success : AppTheme.warning

                ColumnLayout {
                    id: statusColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: checkpoint["available"]
                            ? "Use this pane to probe the current model personality, verify instruction following, and stress-test reasoning before or after a run."
                            : "The chat surface is ready, but no checkpoint is currently mounted for inference."
                        color: AppTheme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Repeater {
                            model: [
                                { label: "Checkpoint", value: checkpoint["available"] ? String(checkpoint["name"] || "Ready") : "None", accent: checkpoint["available"] ? AppTheme.success : AppTheme.warning },
                                { label: "Training state", value: (jobs["training"] || {})["stage"] || "idle", accent: AppTheme.accentPrimary },
                                { label: "Turns", value: String(page.messages.length), accent: AppTheme.chartB }
                            ]

                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 64
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
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
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
                Layout.columnSpan: page.width > 1320 ? 2 : 1
                implicitHeight: presetColumn.implicitHeight + frameHeight
                title: "PROMPT STARTERS"

                Column {
                    id: presetColumn
                    width: parent.width
                    spacing: 10

                    Repeater {
                        model: [
                            { title: "Status", desc: "Ask the checkpoint what it currently does best and where it still struggles.", prompt: "Explain what this model is currently optimized for and where it is still weak." },
                            { title: "Reasoning", desc: "Probe chain quality and answer verification.", prompt: "Solve this carefully, show the critical reasoning steps, then verify the final answer: " },
                            { title: "Code", desc: "Test implementation clarity and edge cases.", prompt: "Write a Python function for the following task and explain the edge cases: " },
                            { title: "Instruction", desc: "Check format compliance under tight constraints.", prompt: "Answer clearly and concisely, then give a short bullet list of key points for: " }
                        ]

                        Rectangle {
                            required property var modelData
                            width: parent.width
                            height: starterDesc.implicitHeight + 56
                            radius: 14
                            color: AppTheme.alpha(AppTheme.panelAlt, 0.50)
                            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.08)
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                Row {
                                    width: parent.width
                                    Text { text: modelData.title; color: AppTheme.textPrimary; font.pixelSize: 12; font.weight: Font.DemiBold }
                                    Item { width: parent.width - starterUse.width - 40; height: 1 }
                                    GlassActionButton {
                                        id: starterUse
                                        width: 54
                                        height: 28
                                        text: "Use"
                                        muted: true
                                        onClicked: appendPreset(modelData.prompt)
                                    }
                                }

                                Text {
                                    id: starterDesc
                                    width: parent.width
                                    text: modelData.desc
                                    color: AppTheme.textMuted
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: width > 1320 ? 5 : 1
            columnSpacing: 16
            rowSpacing: 16

            GlowCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.columnSpan: page.width > 1320 ? 3 : 1
                title: "CONVERSATION"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14

                    ListView {
                        id: messageList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 12
                        model: page.messages

                        delegate: Column {
                            width: ListView.view.width
                            spacing: 5
                            required property var modelData

                            readonly property bool isUser: modelData.role === "user"

                            Text {
                                anchors.right: isUser ? parent.right : undefined
                                anchors.left: isUser ? undefined : parent.left
                                anchors.rightMargin: isUser ? 4 : 0
                                anchors.leftMargin: isUser ? 0 : 4
                                text: isUser ? "Operator" : "Model"
                                color: AppTheme.textMuted
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }

                            Rectangle {
                                anchors.right: isUser ? parent.right : undefined
                                anchors.left: isUser ? undefined : parent.left
                                width: Math.min(bubbleText.implicitWidth + 34, parent.width * 0.84)
                                height: bubbleText.implicitHeight + 24
                                radius: 14
                                color: isUser ? AppTheme.alpha(AppTheme.chartB, 0.16) : AppTheme.alpha(AppTheme.panelAlt, 0.54)
                                border.color: isUser ? AppTheme.alpha(AppTheme.chartB, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                                border.width: 1

                                Text {
                                    id: bubbleText
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    text: modelData.text
                                    color: AppTheme.textPrimary
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        footer: Item {
                            width: parent.width
                            height: page.sending ? 36 : 0
                            visible: page.sending

                            RowLayout {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Repeater {
                                    model: 3

                                    Rectangle {
                                        required property int index
                                        width: 6
                                        height: 6
                                        radius: 3
                                        color: AppTheme.accentPrimary

                                        SequentialAnimation on y {
                                            running: page.sending
                                            loops: Animation.Infinite
                                            PauseAnimation { duration: index * 120 }
                                            NumberAnimation { to: -3; duration: 260; easing.type: Easing.OutQuad }
                                            NumberAnimation { to: 0; duration: 300; easing.type: Easing.InQuad }
                                            PauseAnimation { duration: 480 - index * 120 }
                                        }
                                    }
                                }

                                Text {
                                    text: "Generating..."
                                    color: AppTheme.textMuted
                                    font.pixelSize: 11
                                }
                            }
                        }

                        onCountChanged: Qt.callLater(function() { messageList.positionViewAtEnd() })
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            height: 48
                            radius: 14
                            color: AppTheme.alpha(AppTheme.panelAlt, 0.56)
                            border.color: chatInput.activeFocus ? AppTheme.alpha(AppTheme.accentPrimary, 0.28) : AppTheme.alpha(AppTheme.accentGlass, 0.08)
                            border.width: 1

                            TextInput {
                                id: chatInput
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                verticalAlignment: TextInput.AlignVCenter
                                color: AppTheme.textPrimary
                                font.pixelSize: 13
                                clip: true
                                onAccepted: page.sendMessage()
                            }

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                verticalAlignment: Text.AlignVCenter
                                text: "Ask the current checkpoint something useful..."
                                color: AppTheme.textDim
                                font.pixelSize: 13
                                visible: !chatInput.text.length
                                enabled: false
                            }
                        }

                        GlassActionButton {
                            width: 88
                            height: 48
                            text: "Clear"
                            muted: true
                            onClicked: {
                                page.messages = []
                                messageList.model = []
                            }
                        }

                        GlassActionButton {
                            width: 94
                            height: 48
                            text: page.sending ? "..." : "Send"
                            primary: true
                            enabled: page.canSendMessage
                            onClicked: page.sendMessage()
                        }
                    }
                }
            }

            GlowCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.columnSpan: page.width > 1320 ? 2 : 1
                title: "GENERATION CONTROLS"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    SectionHeader {
                        Layout.fillWidth: true
                        title: "Sampling"
                        subtitle: "Tune response behavior before you send the next prompt."
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8

                        Text { text: "Temperature"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Text { text: "Max Tokens"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                        StyledField { id: tempField; Layout.fillWidth: true; text: "0.7"; validator: DoubleValidator { bottom: 0.0; top: 5.0 } }
                        StyledField { id: maxTokensField; Layout.fillWidth: true; text: "256"; validator: IntValidator { bottom: 1; top: 8192 } }
                        Text { text: "Top-k"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Text { text: "Top-p"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                        StyledField { id: topKField; Layout.fillWidth: true; text: "50"; validator: IntValidator { bottom: 0; top: 1000 } }
                        StyledField { id: topPField; Layout.fillWidth: true; text: "0.9"; validator: DoubleValidator { bottom: 0.0; top: 1.0 } }
                    }

                    Text { text: "Prompt Mode"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    StyledCombo { id: modeBox; Layout.fillWidth: true; model: ["Balanced", "Reasoning", "Coding", "Strict concise"] }

                    Text { text: "Checkpoint Target"; color: AppTheme.textMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    StyledCombo {
                        id: checkpointBox
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: "Latest available", value: "" },
                            { text: checkpoint["name"] ? String(checkpoint["name"]) : "None", value: checkpoint["path"] || "" }
                        ]
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
