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
    // With the keys held as it was clicked (Shift+click on Record: video).
    signal clicked(int modifiers)

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
    // A short button (a small window) has one line: the label on it, and
    // its key only where both fit side by side.
    readonly property bool roomy: height >= 44
    Text {
        id: labelText
        x: 10
        y: 5
        width: parent.width - 20
        elide: button.roomy ? Text.ElideNone : Text.ElideRight
        maximumLineCount: button.roomy ? 3 : 1
        // Arrows and key symbols are the console's own drawings.
        readonly property color ink: button.lit ? "#0a0806" : button.color
        textFormat: backend.hasIcons(button.label) ? Text.StyledText : Text.PlainText
        text: textFormat === Text.StyledText ? backend.rich(button.label, ink, button.labelSize) : button.label
        color: ink
        font.family: "Noto Sans Mono"
        font.pixelSize: button.labelSize
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        wrapMode: button.roomy ? Text.Wrap : Text.NoWrap
    }
    Text {
        id: keyText
        visible: button.key !== "" && (button.roomy || labelText.implicitWidth + implicitWidth + 28 < button.width)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        readonly property color ink: button.lit ? "#0a0806" : button.color
        textFormat: backend.hasIcons(button.key) ? Text.StyledText : Text.PlainText
        text: textFormat === Text.StyledText ? backend.rich(backend.keys(button.key), ink, 11) : backend.keys(button.key)
        color: ink
        opacity: 0.7
        font.family: "Noto Sans Mono"
        font.pixelSize: 11
        font.capitalization: Font.AllUppercase
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: mouse => button.clicked(mouse.modifiers)
    }
}
