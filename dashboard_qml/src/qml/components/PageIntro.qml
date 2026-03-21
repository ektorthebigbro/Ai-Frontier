import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string title: ""
    property string subtitle: ""

    spacing: 10
    Layout.fillWidth: true

    RowLayout {
        visible: root.title.length > 0
        spacing: 10

        Rectangle {
            width: 28
            height: 2
            radius: 0
            color: AppTheme.accentPrimary
            opacity: 0.92
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: root.title.toUpperCase()
            color: AppTheme.textMuted
            font.pixelSize: 10
            font.weight: 800
            font.letterSpacing: 2.0
            Layout.alignment: Qt.AlignVCenter
        }
    }

    Text {
        text: root.title
        color: AppTheme.textPrimary
        font.pixelSize: 38
        font.weight: 800
        font.letterSpacing: -1.0
        Layout.fillWidth: true
        Layout.maximumWidth: 900
        wrapMode: Text.WordWrap
    }

    Text {
        text: root.subtitle
        color: AppTheme.textSecondary
        font.pixelSize: 15
        lineHeight: 1.15
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.maximumWidth: 760
    }
}
