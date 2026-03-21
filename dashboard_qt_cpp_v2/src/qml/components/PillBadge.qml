import QtQuick

Rectangle {
    id: badge

    height: 20
    radius: 6

    property string text: ""
    property color badgeColor: AppTheme.accentSecondary

    width: Math.max(54, label.implicitWidth + 18)
    color: AppTheme.alpha(badgeColor, 0.10)
    border.color: AppTheme.alpha(badgeColor, 0.22)
    border.width: 1

    Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: 160 } }
    Behavior on border.color { ColorAnimation { duration: 160 } }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(1, 1, 1, 0.10)
        opacity: 0.55
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        color: badge.badgeColor
        font.pixelSize: 10
        font.weight: 800
        font.letterSpacing: 0.55
    }
}
