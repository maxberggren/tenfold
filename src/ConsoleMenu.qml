import QtQuick
import QtQuick.Controls

// A menu as the console draws its own (the Song page's right-click menu,
// ArrangeControl::paintMenu): near-black glass, a thin line in the colour,
// a dim title over a rule, and rows that fill with the colour under the
// mouse, each its key at the right. `items` is a list of
// { label, key, danger, action } (action: a function); a row with
// label "" is a rule between groups.
Popup {
    id: menu
    property string title: ""
    property color color: "#FFB000"
    property var items: []
    readonly property color red: "#FF4136"

    padding: 3
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Item {
        // A shadow, as the console's menu has.
        Rectangle { x: 3; y: 3; width: parent.width; height: parent.height; color: Qt.rgba(0, 0, 0, 0.55) }
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(10 / 255, 9 / 255, 8 / 255, 0.97)
            border.color: menu.color
            border.width: 1.5
        }
    }

    contentItem: Column {
        width: menu.width - 2 * menu.padding
        Item {
            visible: menu.title !== ""
            width: parent.width
            height: 24
            Text {
                x: 7
                anchors.verticalCenter: parent.verticalCenter
                text: menu.title
                color: menu.color
                opacity: 0.65
                font.family: "Noto Sans Mono"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
            }
            Rectangle {
                anchors.bottom: parent.bottom
                x: 3
                width: parent.width - 6
                height: 1
                color: menu.color
                opacity: 0.3
            }
        }
        Repeater {
            model: menu.items
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool rule: modelData.label === ""
                readonly property color tint: modelData.danger ? menu.red : menu.color
                width: parent.width
                height: rule ? 9 : 28
                Rectangle {
                    visible: row.rule
                    anchors.verticalCenter: parent.verticalCenter
                    x: 3
                    width: parent.width - 6
                    height: 1
                    color: menu.color
                    opacity: 0.3
                }
                Rectangle {
                    visible: !row.rule
                    anchors.fill: parent
                    anchors.topMargin: 2
                    color: rowMouse.containsMouse ? row.tint : "transparent"
                }
                Text {
                    visible: !row.rule
                    x: 9
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 1
                    text: row.modelData.label
                    color: rowMouse.containsMouse ? "#0a0806" : row.tint
                    font.family: "Noto Sans Mono"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                }
                Text {
                    visible: !row.rule && (row.modelData.key || "") !== ""
                    anchors.right: parent.right
                    anchors.rightMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 1
                    text: backend.keys(row.modelData.key || "")
                    color: rowMouse.containsMouse ? "#0a0806" : menu.color
                    opacity: rowMouse.containsMouse ? 1 : 0.45
                    font.family: "Noto Sans Mono"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    enabled: !row.rule
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.close()
                        row.modelData.action()
                    }
                }
            }
        }
    }
}
