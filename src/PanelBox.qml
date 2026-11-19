import QtQuick

// A framed section of the panel: a phosphor outline with its title cut into
// the top edge, like the boxes on an old touch-screen control console.
Item {
    id: box
    property string title: ""
    property color color: "#FFB000"
    property color background: "#000000"
    property int stroke: 2
    readonly property bool hasTitle: title !== ""
    default property alias content: inner.data

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: box.hasTitle ? titleLabel.height / 2 : 0
        color: "transparent"
        border.color: box.color
        border.width: box.stroke
    }
    Rectangle {
        visible: box.hasTitle
        x: 14
        width: titleLabel.implicitWidth + 12
        height: titleLabel.height
        color: box.background
        Text {
            id: titleLabel
            x: 6
            text: box.title
            color: box.color
            font.family: "Noto Sans Mono"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 1.5
            font.capitalization: Font.AllUppercase
        }
    }
    Item {
        id: inner
        anchors.fill: parent
        anchors.topMargin: (box.hasTitle ? titleLabel.height : 0) + 6
        anchors.margins: 10
    }
}
