import QtQuick

Item {
    id: indicator

    property bool connected: false
    property string label: ""

    implicitWidth: badge.width
    implicitHeight: badge.height

    Rectangle {
        id: badge
        width: row.width + 26
        height: 28
        radius: 8
        color: indicator.connected
            ? AppTheme.alpha(AppTheme.successStrong, 0.10)
            : AppTheme.alpha(AppTheme.dangerStrong, 0.10)
        border.color: indicator.connected
            ? AppTheme.alpha(AppTheme.success, 0.22)
            : AppTheme.alpha(AppTheme.danger, 0.22)
        border.width: 1

        Behavior on color { ColorAnimation { duration: 200 } }
        Behavior on border.color { ColorAnimation { duration: 200 } }

        Rectangle {
            width: 3
            height: parent.height - 10
            radius: 1
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            color: indicator.connected ? AppTheme.success : AppTheme.danger
        }

        Row {
            id: row
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 2
            spacing: 8

            Rectangle {
                id: statusDot
                width: 7
                height: 7
                radius: 2
                color: indicator.connected ? AppTheme.success : AppTheme.danger
                anchors.verticalCenter: parent.verticalCenter
                scale: 1.0

                Behavior on color { ColorAnimation { duration: 200 } }

                SequentialAnimation {
                    id: statusPulse
                    NumberAnimation { target: statusDot; property: "scale"; from: 1.0; to: 1.38; duration: 140; easing.type: Easing.OutCubic }
                    NumberAnimation { target: statusDot; property: "scale"; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
                }
            }

            Text {
                text: indicator.label
                color: indicator.connected ? AppTheme.success : AppTheme.danger
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 0.4
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    onConnectedChanged: statusPulse.restart()
}
