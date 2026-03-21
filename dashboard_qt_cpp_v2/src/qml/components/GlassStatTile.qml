import QtQuick
import AiFrontier

GlassPanel {
    id: root

    property string label: ""
    property string value: "--"
    property string detail: ""
    property color accent: AppTheme.textMuted
    property int valueSize: 24

    radius: 10

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.topMargin: 14
        width: 32
        height: 2
        radius: 1
        color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.78)
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 16
        anchors.bottomMargin: 14
        spacing: 7

        Row {
            spacing: 8

            Text {
                text: root.label
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.2
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            text: root.value
            color: AppTheme.textPrimary
            font.pixelSize: root.valueSize
            font.weight: Font.Bold
            font.letterSpacing: -0.5
            elide: Text.ElideRight
            width: parent.width
        }

        Text {
            visible: root.detail.length > 0
            text: root.detail
            color: AppTheme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            width: parent.width
        }
    }
}
