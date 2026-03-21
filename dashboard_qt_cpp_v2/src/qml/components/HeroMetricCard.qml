import QtQuick
import QtQuick.Effects
import AiFrontier

GlassPanel {
    id: root

    height: 136
    radius: 18

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
        shadowBlur: 0.58
        shadowVerticalOffset: 10
    }

    HoverHandler { id: hoverArea }
    highlighted: hoverArea.hovered

    Row {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Rectangle {
            width: 50
            height: 50
            radius: 14
            color: Qt.rgba(root.iconColor.r, root.iconColor.g, root.iconColor.b, 0.15)
            border.color: Qt.rgba(root.iconColor.r, root.iconColor.g, root.iconColor.b, 0.24)
            border.width: 1
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.06) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            Text {
                anchors.centerIn: parent
                text: root.icon
                font.pixelSize: 18
                color: root.iconColor
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            width: parent.width - 72

            Text {
                text: root.title
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.weight: 800
                font.letterSpacing: 1.2
            }

            Text {
                text: root.value
                color: AppTheme.textPrimary
                font.pixelSize: 30
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
                    radius: 11
                    width: chipLabel.implicitWidth + 18
                    height: 22
                    color: Qt.rgba(1, 1, 1, 0.05)
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    border.width: 1

                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: root.chip
                        color: AppTheme.textSecondary
                        font.pixelSize: 10
                        font.weight: 700
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
