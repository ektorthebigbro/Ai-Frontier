import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import AiFrontier

GlassPanel {
    id: root

    property string title: ""
    property string badge: ""
    property color badgeColor: AppTheme.accentPrimary
    property int contentPadding: 22

    default property alias contentData: contentSlot.data

    radius: 18
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.55)
        shadowBlur: 0.74
        shadowVerticalOffset: 12
        shadowHorizontalOffset: 0
    }

    HoverHandler { id: hoverArea }
    highlighted: hoverArea.hovered

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: parent.width * 0.44
        height: 2
        radius: 1
        color: AppTheme.alpha(root.badgeColor, root.title.length ? 0.52 : 0.22)
        opacity: root.highlighted ? 1.0 : 0.72
    }

    Item {
        id: cardHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.title.length > 0 ? 62 : 0
        visible: root.title.length > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            spacing: 10

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 8
                height: 8
                radius: 4
                color: root.badgeColor
                opacity: 0.90
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.title
                color: AppTheme.textSecondary
                font.pixelSize: 10
                font.weight: 800
                font.letterSpacing: 1.8
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                visible: root.badge.length > 0
                Layout.alignment: Qt.AlignVCenter
                radius: 11
                implicitWidth: badgeLabel.implicitWidth + 20
                height: 22
                color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.13)
                border.color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.26)
                border.width: 1

                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: root.badge
                    color: root.badgeColor
                    font.pixelSize: 10
                    font.weight: 700
                    font.letterSpacing: 0.3
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            height: 1
            color: Qt.rgba(1, 1, 1, 0.055)
        }
    }

    Item {
        id: contentSlot
        anchors.top: cardHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.contentPadding
        anchors.rightMargin: root.contentPadding
        anchors.topMargin: root.title.length > 0 ? root.contentPadding - 4 : root.contentPadding
        anchors.bottomMargin: root.contentPadding
    }
}
