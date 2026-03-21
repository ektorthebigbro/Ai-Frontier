import QtQuick
import AiFrontier

GlassPanel {
    id: chart

    property var series1: []
    property var series2: []
    property color color1: Qt.rgba(0.93, 0.96, 0.99, 0.88)
    property color color2: Qt.rgba(0.66, 0.79, 1.0, 0.62)
    property string label1: "Series 1"
    property string label2: "Series 2"
    property real minVal: 0.0
    property real maxVal: 1.0
    property bool autoRange: true

    radius: 16

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 16

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            if (!chart.series1 || chart.series1.length < 2)
                return

            var all = chart.series1.slice()
            if (chart.series2 && chart.series2.length > 0)
                all = all.concat(chart.series2)

            var mn
            var mx
            if (chart.autoRange) {
                mn = Math.min.apply(null, all)
                mx = Math.max.apply(null, all)
                var rng = mx - mn
                mn -= rng * 0.10
                mx += rng * 0.12
            } else {
                mn = chart.minVal
                mx = chart.maxVal
            }
            if (mn === mx) {
                mn = 0
                mx = 1
            }

            var top = height * 0.08
            var bot = height * 0.88
            var chartH = bot - top

            function norm(v) { return (v - mn) / (mx - mn) }
            function xp(i, n) { return (i / (n - 1)) * width }
            function yp(v) { return bot - norm(v) * chartH }

            ctx.strokeStyle = "rgba(255,255,255,0.05)"
            ctx.lineWidth = 1
            for (var gi = 0; gi <= 4; gi++) {
                ctx.beginPath()
                ctx.moveTo(0, top + (chartH / 4) * gi)
                ctx.lineTo(width, top + (chartH / 4) * gi)
                ctx.stroke()
            }

            function drawSeries(data, col) {
                if (!data || data.length < 2)
                    return

                var r = col.r
                var g = col.g
                var b = col.b

                var areaGrad = ctx.createLinearGradient(0, top, 0, bot)
                areaGrad.addColorStop(0, "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + ",0.13)")
                areaGrad.addColorStop(0.55, "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + ",0.04)")
                areaGrad.addColorStop(1, "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + ",0)")

                ctx.beginPath()
                for (var i = 0; i < data.length; i++) {
                    var x = xp(i, data.length)
                    var y = yp(data[i])
                    if (i === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
                ctx.lineTo(xp(data.length - 1, data.length), bot)
                ctx.lineTo(0, bot)
                ctx.closePath()
                ctx.fillStyle = areaGrad
                ctx.fill()

                ctx.beginPath()
                ctx.strokeStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + ",0.90)"
                ctx.lineWidth = 2.0
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                for (var j = 0; j < data.length; j++) {
                    var lx = xp(j, data.length)
                    var ly = yp(data[j])
                    if (j === 0)
                        ctx.moveTo(lx, ly)
                    else
                        ctx.lineTo(lx, ly)
                }
                ctx.stroke()

                var ex = xp(data.length - 1, data.length)
                var ey = yp(data[data.length - 1])
                ctx.beginPath()
                ctx.arc(ex, ey, 3.2, 0, 2 * Math.PI)
                ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + ",0.96)"
                ctx.fill()
            }

            drawSeries(chart.series1, chart.color1)
            drawSeries(chart.series2, chart.color2)
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Connections {
        target: chart
        function onSeries1Changed() { canvas.requestPaint() }
        function onSeries2Changed() { canvas.requestPaint() }
    }

    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 14
        spacing: 14

        Repeater {
            model: [
                { label: chart.label1, color: chart.color1, show: true },
                { label: chart.label2, color: chart.color2, show: chart.series2 && chart.series2.length > 0 }
            ]

            Row {
                required property var modelData
                visible: modelData.show
                spacing: 6

                Rectangle {
                    width: 16
                    height: 2
                    radius: 1
                    color: modelData.color
                    opacity: 0.78
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: modelData.label
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }
        }
    }
}
