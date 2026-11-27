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
    // Asks to be pressed: lit, breathing, with a glow round it.
    property bool beckon: false
    // A smaller line under the label (where the button is tall enough).
    property string note: ""
    // How far the glow reaches out round it.
    property int glowSpread: 11
    // With the keys held as it was clicked (Shift+click on Record: video).
    signal clicked(int modifiers)

    // Lit while held down, too (pressLights false: a button whose click
    // lights it anyway, so the colour and the words change together).
    property bool pressLights: true
    readonly property bool lit: active || (mouse.pressed && pressLights) || beckon

    // The glow: two rings in the button's colour, breathing with the fill.
    Repeater {
        model: button.beckon ? 2 : 0
        Rectangle {
            required property int index
            anchors.fill: parent
            anchors.margins: -Math.round(button.glowSpread * (index === 0 ? 0.45 : 1))
            color: "transparent"
            border.color: button.color
            border.width: 3
            opacity: (index === 0 ? 0.7 : 0.35) * (0.35 + 0.65 * button.breath)
        }
    }
    // Steady: a beckoning button is lit and ringed, without breathing.
    readonly property real breath: 1

    Rectangle {
        visible: button.hint
        anchors.fill: parent
        anchors.margins: -6
        color: "transparent"
        border.color: "#FFFFFF"
        border.width: 3
    }

    Rectangle {
        anchors.fill: parent
        color: button.lit ? button.color : "transparent"
        border.color: button.color
        border.width: 2
        // Beckoning, the fill breathes (never so dim that the dark letters
        // stop reading); pressed, it's full.
        Rectangle {
            visible: button.beckon && !mouse.pressed
            anchors.fill: parent
            color: "#000000"
            opacity: 0.4 * (1 - button.breath)
        }
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
        // Room for a line of it under the label (13 px type, ~18 px a line);
        // reckoned from the label, not from this text's own size, which
        // made a binding loop that flickered.
        visible: button.note !== "" && button.roomy && labelText.y + labelText.height + 4 + 18 < button.height - 4
        x: 10
        y: labelText.y + labelText.height + 4
        width: parent.width - 20
        text: button.note
        color: labelText.ink
        font.family: "Noto Sans Mono"
        font.pixelSize: 13
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
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
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => button.clicked(mouse.modifiers)
    }
}
