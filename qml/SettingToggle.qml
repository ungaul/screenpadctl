import QtQuick

Item {
    id: root

    property string label
    property bool checked
    property color textColor
    property color onColor
    property color offColor
    property color knobOnColor
    property color knobOffColor

    signal toggled()

    Text {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        text: root.label
        color: root.textColor
        font.pixelSize: 18
    }

    Rectangle {
        id: pill
        width: 64; height: 36; radius: 18
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        color: root.checked ? root.onColor : root.offColor

        Rectangle {
            width: 28; height: 28; radius: 14
            x: root.checked ? pill.width - width - 4 : 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? root.knobOnColor : root.knobOffColor
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        TapHandler { onTapped: root.toggled() }
    }
}
