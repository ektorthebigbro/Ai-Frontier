import QtQuick
import AiFrontier

GlassPanel {
    id: root

    property string label: ""
    property string value: "--"
    property string detail: ""
    property color accent: AppTheme.textMuted
    property int valueSize: 24

    radius: 16

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 18
        anchors.topMargin: 15
        width: 24
        height: 3
        radius: 1.5
        color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.78)
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 18
        anchors.bottomMargin: 16
        spacing: 8

        Row {
            spacing: 8

            Rectangle {
                width: 7
                height: 7
                radius: 3.5
                color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.92)
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.label
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.0
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
