pragma ComponentBehavior: Bound

import QtQuick
import AiFrontier

Item {
    id: root

    default property alias contentData: content.data

    property int radius: 10
    property int contentPadding: 0
    property bool highlighted: false

    property vector2d _scenePos: Qt.vector2d(0, 0)
    function _updateScenePos() {
        const p = root.mapToItem(null, 0, 0)
        root._scenePos = Qt.vector2d(p.x, p.y)
    }
    onXChanged: _updateScenePos()
    onYChanged: _updateScenePos()
    onVisibleChanged: if (visible) _updateScenePos()
    Component.onCompleted: _updateScenePos()

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(1, 1, 1, root.highlighted ? 0.046 : 0.030)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.highlighted ? 0.16 : 0.10)
    }

    ShaderEffect {
        anchors.fill: shell
        property var backdrop: AppTheme.sceneBackdropSource
        property real time: 0.0
        property real highlightAmount: root.highlighted ? 0.26 : 0.11
        property vector2d resolution: Qt.vector2d(width, height)
        property vector2d scenePos: root._scenePos
        property vector2d sceneSize: Qt.vector2d(width, height)
        property vector2d viewportSize: AppTheme.sceneBackdropSize
        property vector2d backdropTextureSize: AppTheme.sceneBackdropTextureSize
        property vector2d cornerData: Qt.vector2d(root.radius, 0)
        fragmentShader: "qrc:/qt/qml/AiFrontier/shaders/frosted_glass.frag.qsb"
        vertexShader: "qrc:/qt/qml/AiFrontier/shaders/pass.vert.qsb"
        opacity: AppTheme.sceneBackdropSource ? 1.0 : 0.0
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        visible: !AppTheme.sceneBackdropSource
        color: AppTheme.alpha(AppTheme.panel, 0.96)
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.highlighted ? 0.10 : 0.055)
        color: "transparent"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(1, 1, 1, root.highlighted ? 0.16 : 0.10)
        opacity: 0.8
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        radius: root.radius
        color: Qt.rgba(1, 1, 1, 0.05)
        opacity: 0.6
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.00) }
            GradientStop { position: 0.72; color: Qt.rgba(0, 0, 0, 0.04) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.10) }
        }
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: root.contentPadding
    }
}
