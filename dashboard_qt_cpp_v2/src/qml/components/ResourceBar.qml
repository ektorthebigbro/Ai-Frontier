import QtQuick

Column {
    id: resourceBar
    spacing: 8

    property string label: ""
    property real value: 0.0
    property string detail: ""
    property color barColor: AppTheme.accentSecondary
    property real displayedValue: 0.0

    onValueChanged: displayedValue = value
    Behavior on displayedValue { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

    Row {
        width: parent.width

        Text {
            id: labelText
            text: resourceBar.label
            color: AppTheme.textSecondary
            font.pixelSize: 11
        }

        Item { width: parent.width - labelText.width - valueText.width; height: 1 }

        Text {
            id: valueText
            text: Math.round(resourceBar.displayedValue) + "%"
            color: AppTheme.textPrimary
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    Rectangle {
        width: parent.width
        height: 12
        radius: 6
        color: AppTheme.alpha(AppTheme.panelStrong, 0.62)
        border.color: AppTheme.alpha(AppTheme.accentGlass, 0.10)
        border.width: 1

        Rectangle {
            width: Math.max(parent.radius * 2, parent.width * (resourceBar.displayedValue / 100))
            height: parent.height
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: resourceBar.barColor }
                GradientStop { position: 1.0; color: Qt.lighter(resourceBar.barColor, 1.15) }
            }
            Behavior on width { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
        }
    }

    Text {
        visible: resourceBar.detail.length > 0
        text: resourceBar.detail
        color: AppTheme.textMuted
        font.pixelSize: 10
    }
}
