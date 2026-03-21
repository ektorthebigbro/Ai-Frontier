import QtQuick
import QtQuick.Layouts

GlowCard {
    id: root

    required property string titleText
    required property string stageText
    required property string progressText
    required property string messageText
    required property real progressValue
    required property string metaText
    required property bool running
    required property bool paused
    required property var actionItems

    signal actionTriggered(string path)

    readonly property color stateColor: root.stageText === "failed"
        ? AppTheme.danger
        : (root.paused ? AppTheme.warning : (root.running ? AppTheme.success : AppTheme.accentPrimary))

    Layout.fillWidth: true
    implicitHeight: jobContent.implicitHeight + frameHeight
    title: root.titleText
    badge: root.stageText
    badgeColor: root.stateColor

    Column {
        id: jobContent
        width: parent.width
        spacing: 12

        Rectangle {
            width: parent.width
            implicitHeight: heroRow.implicitHeight + 22
            radius: 14
            color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.10)
            border.color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.20)
            border.width: 1

            RowLayout {
                id: heroRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 12
                    color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.16)
                    border.color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.22)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: root.running ? "\u25b6" : (root.paused ? "\u23f8" : "\u25ce")
                        color: root.stateColor
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: root.progressText + " complete"
                        color: AppTheme.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: root.metaText
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: root.messageText
            color: AppTheme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        GradientProgressBar {
            width: parent.width
            value: root.progressValue
            startColor: root.stateColor
            endColor: root.running ? AppTheme.chartC : AppTheme.accentSecondary
        }

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.actionItems

                GlassActionButton {
                    required property var modelData
                    width: Math.max(84, text.length * 7 + 34)
                    height: 36
                    text: modelData.label
                    primary: modelData.kind === "primary"
                    danger: modelData.kind === "danger"
                    muted: modelData.kind === "muted"
                    onClicked: root.actionTriggered(modelData.path)
                }
            }
        }
    }
}
