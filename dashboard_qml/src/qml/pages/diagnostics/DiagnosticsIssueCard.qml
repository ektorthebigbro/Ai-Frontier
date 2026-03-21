import QtQuick

Rectangle {
    id: root

    required property var issueData
    property string moduleText: ""
    property string severityText: "error"
    property string seenText: "unknown"
    property string issueText: ""
    property string issueMetaText: ""

    readonly property color severityColor: root.severityText === "error"
        ? AppTheme.danger
        : (root.severityText === "warning" ? AppTheme.warning : AppTheme.accentPrimary)

    signal clearRequested(string key)
    signal reloadRequested(string module)
    signal diveRequested(var issue)

    width: parent ? parent.width : 0
    height: issueColumn.implicitHeight + 24
    radius: 14
    color: Qt.rgba(root.severityColor.r, root.severityColor.g, root.severityColor.b, 0.08)
    border.color: Qt.rgba(root.severityColor.r, root.severityColor.g, root.severityColor.b, 0.20)
    border.width: 1

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        radius: 1.5
        color: root.severityColor
        opacity: 0.9
    }

    Column {
        id: issueColumn
        anchors.fill: parent
        anchors.margins: 14
        anchors.leftMargin: 16
        spacing: 10

        Row {
            width: parent.width

            Text {
                id: moduleLabel
                visible: !!root.issueData.module
                text: root.moduleText
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Item {
                width: Math.max(0, parent.width - severityBadge.width - seenLabel.width - (moduleLabel.visible ? moduleLabel.width + 12 : 0))
                height: 1
            }

            FlatTag {
                id: severityBadge
                text: root.severityText
                tagColor: root.severityColor
            }

            Text {
                id: seenLabel
                text: "Seen " + root.seenText
                color: AppTheme.textMuted
                font.pixelSize: 10
            }
        }

        Text {
            width: parent.width
            text: root.issueText
            color: AppTheme.textPrimary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            visible: root.issueMetaText.length > 0
            width: parent.width
            text: root.issueMetaText
            color: AppTheme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: 8

            GlassActionButton {
                visible: !!root.issueData.key
                width: 68
                height: 32
                text: "Clear"
                muted: true
                onClicked: root.clearRequested(root.issueData.key)
            }

            GlassActionButton {
                visible: !!root.issueData.module
                width: 74
                height: 32
                text: "Reload"
                muted: true
                onClicked: root.reloadRequested(root.issueData.module)
            }

            GlassActionButton {
                width: 92
                height: 32
                text: "Deep Dive"
                primary: true
                onClicked: root.diveRequested(root.issueData)
            }
        }
    }
}
