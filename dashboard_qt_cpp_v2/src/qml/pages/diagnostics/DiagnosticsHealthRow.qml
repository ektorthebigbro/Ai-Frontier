import QtQuick

Rectangle {
    id: root

    property string status: "info"
    property string checkName: "Check"
    property string messageText: ""

    readonly property color stateColor: root.status === "error"
        ? AppTheme.danger
        : (root.status === "warning" ? AppTheme.warning : AppTheme.accentPrimary)

    width: parent ? parent.width : 0
    implicitHeight: rowLayout.implicitHeight + 20
    radius: 12
    color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.08)
    border.color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.18)
    border.width: 1

    Row {
        id: rowLayout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: root.stateColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            width: parent.width - statusBadge.width - 36
            text: root.checkName + (root.messageText ? " - " + root.messageText : "")
            color: AppTheme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            anchors.verticalCenter: parent.verticalCenter
        }

        PillBadge {
            id: statusBadge
            text: root.status || "info"
            badgeColor: root.stateColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
