import QtQuick

Item {
    id: bar

    height: 40

    property real value: 0.0
    property string label: ""
    property color startColor: AppTheme.accentPrimary
    property color endColor: AppTheme.accentSecondary

    property real _displayed: 0.0
    Behavior on _displayed { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
    onValueChanged: _displayed = Math.max(0, Math.min(1, value))

    Column {
        anchors.fill: parent
        spacing: 8

        Row {
            width: parent.width

            Text {
                id: labelText
                text: bar.label
                color: AppTheme.textSecondary
                font.pixelSize: 11
                visible: bar.label.length > 0
            }

            Item {
                width: parent.width - (bar.label.length > 0 ? labelMeasure.width : 0) - pctText.width
                height: 1
            }

            Text {
                id: pctText
                text: Math.round(bar._displayed * 100) + "%"
                color: AppTheme.textPrimary
                font.pixelSize: 11
                font.weight: Font.Bold
            }

            Text {
                id: labelMeasure
                text: bar.label
                font.pixelSize: 11
                visible: false
            }
        }

        Rectangle {
            width: parent.width
            height: 10
            radius: 5
            color: AppTheme.alpha(AppTheme.panelStrong, 0.72)
            border.color: AppTheme.alpha(AppTheme.accentGlass, 0.10)
            border.width: 1

            Rectangle {
                width: bar._displayed > 0 ? Math.max(height, parent.width * bar._displayed) : 0
                height: parent.height
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: bar.startColor }
                    GradientStop { position: 1.0; color: bar.endColor }
                }
                Behavior on width { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.18) }
                        GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.06) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }
            }
        }
    }
}
