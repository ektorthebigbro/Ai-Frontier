import QtQuick

Item {
    id: toast
    width: 360
    height: visible ? toastRect.height : 0
    opacity: visible ? 1.0 : 0.0
    y: visible ? 0 : 16
    visible: false

    property string currentType: "info"

    function show(message, type) {
        currentType = type || "info"
        toastMessage.text = message
        visible = true
        hideTimer.restart()
    }

    function toneColor() {
        if (toast.currentType === "error")
            return AppTheme.danger
        if (toast.currentType === "success")
            return AppTheme.success
        return AppTheme.accentPrimary
    }

    Rectangle {
        id: toastRect
        width: parent.width
        height: toastMessage.implicitHeight + 30
        radius: 14
        color: AppTheme.alpha(AppTheme.panelAlt, 0.92)
        border.color: AppTheme.alpha(toneColor(), 0.26)
        border.width: 1

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.08) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 4
            radius: 2
            anchors.margins: 12
            color: toneColor()
        }

        Row {
            anchors.fill: parent
            anchors.margins: 14
            anchors.leftMargin: 18
            spacing: 12

            Rectangle {
                width: 30
                height: 30
                radius: 11
                color: AppTheme.alpha(toneColor(), 0.12)
                border.color: AppTheme.alpha(toneColor(), 0.24)
                border.width: 1
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: toast.currentType === "error" ? "!"
                        : toast.currentType === "success" ? "\u2713"
                        : "i"
                    color: toneColor()
                    font.pixelSize: 14
                    font.weight: 700
                }
            }

            Text {
                id: toastMessage
                width: parent.width - 44
                anchors.verticalCenter: parent.verticalCenter
                color: AppTheme.textPrimary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    Timer {
        id: hideTimer
        interval: 3600
        onTriggered: toast.visible = false
    }

    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
}
