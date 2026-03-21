import QtQuick
import QtQuick.Effects
import AiFrontier

Item {
    id: root
    anchors.fill: parent

    readonly property real backdropScale: 0.42
    readonly property bool backdropAnimating: wallpaperImage.status !== Image.Ready

    readonly property vector2d backdropTextureVector: Qt.vector2d(
        Math.max(1, Math.round(root.width * root.backdropScale)),
        Math.max(1, Math.round(root.height * root.backdropScale))
    )

    Item {
        id: backdropScene
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: AppTheme.windowBase
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#121a24" }
                GradientStop { position: 0.45; color: "#0a0f16" }
                GradientStop { position: 1.0; color: "#04070c" }
            }
        }

        Image {
            id: wallpaperImage
            anchors.fill: parent
            source: "qrc:/qt/qml/AiFrontier/assets/bg/dark_bg.jpg"
            fillMode: Image.PreserveAspectCrop
            smooth: true
            asynchronous: true
            opacity: 0.58
        }

        Rectangle {
            width: parent.width * 0.92
            height: parent.height * 0.44
            x: parent.width * 0.34
            y: -parent.height * 0.06
            rotation: -28
            color: Qt.rgba(0.02, 0.04, 0.07, 0.54)
        }

        Rectangle {
            width: parent.width * 0.72
            height: parent.height * 0.34
            x: -parent.width * 0.06
            y: parent.height * 0.56
            rotation: 18
            color: Qt.rgba(0.03, 0.06, 0.08, 0.44)
        }

        Rectangle {
            width: parent.width * 0.38
            height: parent.height * 1.18
            x: parent.width * 0.36
            y: -parent.height * 0.10
            rotation: -21
            color: Qt.rgba(1, 1, 1, 0.055)
            opacity: 0.34
        }

        Rectangle {
            width: parent.width * 0.24
            height: parent.height * 1.18
            x: parent.width * 0.41
            y: -parent.height * 0.10
            rotation: -21
            color: Qt.rgba(1, 1, 1, 0.045)
            opacity: 0.22
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.03) }
                GradientStop { position: 0.18; color: "transparent" }
                GradientStop { position: 0.82; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.02) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.10) }
                GradientStop { position: 0.50; color: Qt.rgba(0, 0, 0, 0.20) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.36) }
            }
        }
    }

    Item {
        id: blurPipeline
        x: -width - 256
        y: -height - 256
        width: root.backdropTextureVector.x
        height: root.backdropTextureVector.y

        ShaderEffectSource {
            id: rawBackdropCapture
            anchors.fill: parent
            sourceItem: backdropScene
            sourceRect: Qt.rect(0, 0, root.width, root.height)
            live: root.backdropAnimating
            recursive: false
            hideSource: false
            smooth: true
            textureSize: Qt.size(root.backdropTextureVector.x, root.backdropTextureVector.y)
        }

        MultiEffect {
            id: blurredBackdrop
            anchors.fill: parent
            source: rawBackdropCapture
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 0.58
            blurMax: 44
            blurMultiplier: 1.0
            brightness: 0.04
            saturation: -0.04
        }

        ShaderEffectSource {
            id: backdropCapture
            anchors.fill: parent
            sourceItem: blurredBackdrop
            live: root.backdropAnimating
            recursive: false
            hideSource: true
            smooth: true
            textureSize: Qt.size(root.backdropTextureVector.x, root.backdropTextureVector.y)
        }
    }

    Component.onCompleted: {
        AppTheme.sceneBackdropSource = backdropCapture
        AppTheme.sceneBackdropSize = Qt.vector2d(width, height)
        AppTheme.sceneBackdropTextureSize = root.backdropTextureVector
        backdropCapture.scheduleUpdate()
    }

    onWidthChanged: {
        AppTheme.sceneBackdropSize = Qt.vector2d(width, height)
        AppTheme.sceneBackdropTextureSize = root.backdropTextureVector
        backdropCapture.scheduleUpdate()
    }

    onHeightChanged: {
        AppTheme.sceneBackdropSize = Qt.vector2d(width, height)
        AppTheme.sceneBackdropTextureSize = root.backdropTextureVector
        backdropCapture.scheduleUpdate()
    }

    Connections {
        target: wallpaperImage
        function onStatusChanged() {
            if (!root.backdropAnimating)
                backdropCapture.scheduleUpdate()
        }
    }
}
