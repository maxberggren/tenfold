import QtQuick

// A status line with its check box, filled when the condition holds.
Row {
    id: lamp
    property string label: ""
    property bool on: false
    property color color: "#39FF88"
    spacing: 8

    Rectangle {
        width: 16
        height: 16
        anchors.verticalCenter: parent.verticalCenter
        color: lamp.on ? lamp.color : "transparent"
        border.color: lamp.on ? lamp.color : Qt.darker(lamp.color, 1.8)
        border.width: 2
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: lamp.label
        color: lamp.on ? lamp.color : Qt.darker(lamp.color, 1.8)
        font.family: "Noto Sans Mono"
        font.pixelSize: 14
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
    }
}
