import QtQuick

Rectangle {
    id: root

    property string text: ""
    property string iconText: ""
    property bool primary: false
    property bool danger: false
    property bool muted: false
    property bool enabled: true
    property color accentColor: danger ? AppTheme.danger : (primary ? AppTheme.accentPrimary : AppTheme.textSecondary)

    signal clicked()

    width: Math.max(94, labelRow.implicitWidth + 28)
    height: 38
    radius: 0
    color: {
        if (!enabled)
            return Qt.rgba(1, 1, 1, 0.03)
        if (danger)
            return hoverArea.containsMouse ? AppTheme.alpha(AppTheme.danger, 0.14) : AppTheme.alpha(AppTheme.dangerStrong, 0.08)
        if (primary)
            return hoverArea.containsMouse ? AppTheme.alpha(AppTheme.accentPrimary, 0.14) : AppTheme.alpha(AppTheme.accentSecondary, 0.09)
        if (muted)
            return hoverArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.03)
        return hoverArea.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : Qt.rgba(1, 1, 1, 0.032)
    }
    border.color: {
        if (!enabled)
            return AppTheme.alpha(AppTheme.accentGlass, 0.04)
        if (danger)
            return AppTheme.alpha(AppTheme.danger, 0.28)
        if (primary)
            return AppTheme.alpha(AppTheme.accentPrimary, 0.28)
            return AppTheme.alpha(AppTheme.accentGlass, 0.08)
    }
    border.width: 1
    opacity: enabled ? 1.0 : 0.55

    Behavior on color { ColorAnimation { duration: 130 } }
    Behavior on border.color { ColorAnimation { duration: 130 } }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(1, 1, 1, 0.12)
        opacity: 0.45
    }

    Row {
        id: labelRow
        anchors.centerIn: parent
        spacing: iconText.length ? 8 : 0

        Text {
            visible: root.iconText.length > 0
            text: root.iconText
            color: root.enabled ? root.accentColor : AppTheme.textDim
            font.pixelSize: 10
            font.weight: Font.Bold
        }

        Text {
            text: root.text
            color: root.enabled
                ? (root.primary ? AppTheme.textPrimary : (root.danger ? AppTheme.danger : root.accentColor))
                : AppTheme.textDim
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.4
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
