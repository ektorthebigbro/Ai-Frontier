import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: control

    height: collapsed ? 50 : 44
    horizontalPadding: 0
    verticalPadding: 0

    property string iconText: ""
    property string label: ""
    property bool active: false
    property bool collapsed: false

    background: Rectangle {
        radius: 10
        color: control.active
            ? AppTheme.alpha(AppTheme.panelRaised, 0.72)
            : (control.hovered ? AppTheme.alpha(AppTheme.panelAlt, 0.46) : "transparent")
        border.color: control.active
            ? AppTheme.alpha(AppTheme.accentPrimary, 0.18)
            : (control.hovered ? AppTheme.alpha(AppTheme.accentGlass, 0.08) : "transparent")
        border.width: 1

        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }

        Rectangle {
            visible: control.active && !control.collapsed
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 22
            radius: 1
            color: AppTheme.accentPrimary
            opacity: 0.95
        }

        Rectangle {
            visible: control.active
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(1, 1, 1, 0.10)
            opacity: 0.65
        }
    }

    contentItem: Item {
        anchors.fill: parent

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: control.collapsed ? 0 : 12
            anchors.horizontalCenter: control.collapsed ? parent.horizontalCenter : undefined
            spacing: control.collapsed ? 0 : 10

            Behavior on spacing { NumberAnimation { duration: 140 } }

            Rectangle {
                width: control.collapsed ? 34 : 30
                height: control.collapsed ? 34 : 30
                radius: 8
                color: control.active
                    ? AppTheme.alpha(AppTheme.accentPrimary, 0.10)
                    : (control.hovered ? Qt.rgba(1, 1, 1, 0.045) : Qt.rgba(1, 1, 1, 0.025))
                border.color: control.active
                    ? AppTheme.alpha(AppTheme.accentPrimary, 0.18)
                    : AppTheme.alpha(AppTheme.accentGlass, 0.06)
                border.width: 1
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.11)
                    opacity: 0.45
                }

                Text {
                    anchors.centerIn: parent
                    text: control.iconText
                    font.pixelSize: control.collapsed ? 13 : 12
                    color: control.active ? AppTheme.textPrimary : AppTheme.textSecondary
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
            }

            Text {
                visible: !control.collapsed
                text: control.label
                font.pixelSize: 13
                color: control.active ? AppTheme.textPrimary : AppTheme.textSecondary
                font.weight: control.active ? 700 : 500
                opacity: control.collapsed ? 0.0 : 1.0
                anchors.verticalCenter: parent.verticalCenter

                Behavior on opacity { NumberAnimation { duration: 120 } }
                Behavior on color { ColorAnimation { duration: 140 } }
            }

            Text {
                visible: !control.collapsed
                text: control.active ? "\u203a" : "\u00b7"
                font.pixelSize: control.active ? 15 : 16
                color: control.active ? AppTheme.accentPrimary : AppTheme.textDim
                anchors.verticalCenter: parent.verticalCenter
                opacity: control.active ? 1.0 : 0.7

                Behavior on color { ColorAnimation { duration: 140 } }
            }
        }
    }
}
