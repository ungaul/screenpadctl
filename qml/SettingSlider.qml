import QtQuick

Item {
    id: root

    property string label
    property real from: 0
    property real to: 1
    property real value
    property string valueText
    property color textColor
    property color dimColor
    property color trackColor
    property color fillColor
    property color knobColor

    property bool dragging: false
    property real dragValue
    readonly property real shownValue: dragging ? dragValue : value

    signal moved(real value)
    signal released(real value)

    function drag(x) {
        dragging = true
        dragValue = from + Math.max(0, Math.min(1, x / track.width)) * (to - from)
        moved(dragValue)
    }

    function finish() {
        released(dragValue)
        dragging = false
    }

    Text {
        id: labelText
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        width: 200
        text: root.label
        color: root.textColor
        font.pixelSize: 18
        elide: Text.ElideRight
    }

    Text {
        id: valueLabel
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: 70
        horizontalAlignment: Text.AlignRight
        text: root.valueText
        color: root.dimColor
        font.pixelSize: 16
    }

    Item {
        id: track
        readonly property real fraction: (root.shownValue - root.from) / (root.to - root.from)
        anchors {
            left: labelText.right; right: valueLabel.left
            verticalCenter: parent.verticalCenter
            leftMargin: 16; rightMargin: 16
        }
        height: 34

        Rectangle {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: 8; radius: 4
            color: root.trackColor

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: parent.width * track.fraction
                radius: 4
                color: root.fillColor
            }
        }

        Rectangle {
            width: 34; height: 34; radius: 17
            anchors.verticalCenter: parent.verticalCenter
            x: track.fraction * (track.width - width)
            color: root.knobColor
        }

        MouseArea {
            anchors.fill: parent
            onPressed: (mouse) => root.drag(mouse.x)
            onPositionChanged: (mouse) => { if (pressed) root.drag(mouse.x) }
            onReleased: root.finish()
            onCanceled: root.finish()
        }
    }
}
