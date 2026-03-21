import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string title: ""
    property string subtitle: ""
    property color accentColor: AppTheme.accentPrimary

    spacing: 5
    Layout.fillWidth: true

    RowLayout {
        spacing: 10
        Layout.fillWidth: true

        Rectangle {
            width: 24
            height: 2
            radius: 0
            color: root.accentColor
            Layout.alignment: Qt.AlignTop
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            Text {
                text: root.title
                color: AppTheme.textPrimary
                font.pixelSize: 20
                font.weight: Font.Bold
                font.letterSpacing: -0.4
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: AppTheme.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
