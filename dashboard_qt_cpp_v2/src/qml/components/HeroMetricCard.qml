import QtQuick
import QtQuick.Effects
import AiFrontier

GlassPanel {
    id: root

    height: 122
    radius: 12

    property string title: ""
    property string value: "--"
    property string icon: "\u25CF"
    property color iconColor: AppTheme.accentPrimary
    property string trend: ""
    property string chip: ""

    readonly property bool up: trend.charAt(0) === "+" || trend.charAt(0) === "\u2191"
    readonly property bool down: trend.charAt(0) === "-" || trend.charAt(0) === "\u2193"

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.48)
        shadowBlur: 0.38
        shadowVerticalOffset: 8
    }

    HoverHandler { id: hoverArea }
    highlighted: hoverArea.hovered

    Row {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Rectangle {
            width: 42
            height: 42
            radius: 8
            color: Qt.rgba(root.iconColor.r, root.iconColor.g, root.iconColor.b, 0.10)
            border.color: Qt.rgba(root.iconColor.r, root.iconColor.g, root.iconColor.b, 0.18)
            border.width: 1
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Qt.rgba(1, 1, 1, 0.11)
                opacity: 0.45
            }

            Text {
                anchors.centerIn: parent
                text: root.icon
                font.pixelSize: 16
                color: root.iconColor
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            width: parent.width - 58

            Text {
                text: root.title
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.weight: 800
                font.letterSpacing: 1.4
            }

            Text {
                text: root.value
                color: AppTheme.textPrimary
                font.pixelSize: 28
                font.weight: 800
                font.letterSpacing: -0.7
                elide: Text.ElideRight
                width: parent.width
            }

            Row {
                spacing: 8
                visible: root.chip.length > 0 || root.trend.length > 0

                Rectangle {
                    visible: root.chip.length > 0
                    radius: 0
                    width: chipLabel.implicitWidth + 14
                    height: 19
                    color: Qt.rgba(1, 1, 1, 0.035)
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 1

                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: root.chip
                        color: AppTheme.textSecondary
                        font.pixelSize: 9
                        font.weight: 700
                        font.letterSpacing: 0.35
                    }
                }

                Text {
                    visible: root.trend.length > 0
                    text: root.trend
                    color: root.up ? AppTheme.success : (root.down ? AppTheme.danger : AppTheme.textSecondary)
                    font.pixelSize: 11
                    font.weight: 700
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
