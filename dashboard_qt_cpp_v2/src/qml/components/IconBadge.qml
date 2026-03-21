import QtQuick

Rectangle {
    id: badge
    property string icon: "\u25CF"
    property color badgeColor: AppTheme.accentPrimary
    property int badgeSize: 40

    width: badgeSize
    height: badgeSize
    radius: badgeSize / 2
    color: AppTheme.alpha(badgeColor, 0.12)
    border.color: AppTheme.alpha(badgeColor, 0.24)
    border.width: 1

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.05) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Text {
        anchors.centerIn: parent
        text: badge.icon
        color: badge.badgeColor
        font.pixelSize: badge.badgeSize * 0.42
        font.weight: Font.DemiBold
    }
}
