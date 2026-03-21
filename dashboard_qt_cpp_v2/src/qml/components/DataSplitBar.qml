import QtQuick

Item {
    id: bar
    height: 46

    property var segments: []

    Rectangle {
        id: track
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 18
        radius: 9
        color: AppTheme.alpha(AppTheme.panelStrong, 0.62)
        border.color: AppTheme.alpha(AppTheme.accentGlass, 0.10)
        border.width: 1
    }

    Row {
        id: barRow
        anchors.fill: track
        anchors.margins: 1
        spacing: 2

        Repeater {
            model: bar.segments

            Rectangle {
                height: parent.height
                width: bar.segments.length > 0
                    ? (barRow.width - (bar.segments.length - 1) * 2) * modelData.value
                    : 0
                radius: index === 0 ? 8
                      : index === bar.segments.length - 1 ? 8
                      : 0
                color: modelData.color
            }
        }
    }

    Row {
        anchors.top: track.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 12

        Repeater {
            model: bar.segments

            Row {
                spacing: 6

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: modelData.color
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: modelData.label + " " + Math.round(modelData.value * 100) + "%"
                    color: AppTheme.textSecondary
                    font.pixelSize: 10
                }
            }
        }
    }
}
