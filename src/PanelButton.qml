import QtQuick

// A console button: outlined, label in the top-left corner, its key in the
// bottom-right. Lit (filled, dark text) while it's on.
Item {
    id: button
    property string label: ""
    property string key: ""
    property color color: "#FFB000"
    property bool active: false
    property int labelSize: 16
    property bool hint: false // the tutorial points at it: a breathing halo
    signal clicked()

    readonly property bool lit: active || mouse.pressed

    Rectangle {
        visible: button.hint
        anchors.fill: parent
        anchors.margins: -6
        color: "transparent"
        border.color: "#FFFFFF"
        border.width: 3
        SequentialAnimation on opacity {
            running: button.hint
            loops: Animation.Infinite
            NumberAnimation { to: 0.2; duration: 500 }
            NumberAnimation { to: 1; duration: 500 }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: button.lit ? button.color : "transparent"
        border.color: button.color
        border.width: 2
    }
    Text {
        x: 10
        y: 5
        width: parent.width - 20
        text: button.label
        color: button.lit ? "#0a0806" : button.color
        font.family: "Noto Sans Mono"
        font.pixelSize: button.labelSize
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        wrapMode: Text.Wrap
    }
    Text {
        visible: button.key !== ""
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        text: button.key
        color: button.lit ? "#0a0806" : button.color
        opacity: 0.7
        font.family: "Noto Sans Mono"
        font.pixelSize: 11
        font.capitalization: Font.AllUppercase
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: button.clicked()
    }
}
