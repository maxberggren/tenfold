import QtQuick
import QtQuick.Layouts

// A question the console asks before something it can't simply undo
// (starting over): the window dims, a box in the console's own lines says
// what will happen, and one click answers. Enter confirms, Esc keeps.
Rectangle {
    id: dialog
    property string title: ""
    property string text: ""
    property string confirmLabel: qsTr("Yes")
    property string keepLabel: qsTr("Keep")
    property color tint: "#FF4136"
    signal confirmed()
    signal kept()

    color: Qt.rgba(0, 0, 0, 0.72)
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            dialog.confirmed()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            dialog.kept()
            event.accepted = true
        }
    }
    // A click beside the box keeps things as they are.
    MouseArea {
        anchors.fill: parent
        onClicked: dialog.kept()
    }

    // Solid glass behind the box: nothing of the pad shows through the words.
    Rectangle {
        x: box.x - 6
        y: box.y + 2
        width: box.width + 12
        height: box.height + 4
        color: "#000000"
    }
    PanelBox {
        id: box
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        height: body.implicitHeight + 120
        title: dialog.title
        color: dialog.tint
        // Clicks on the box stay in it.
        MouseArea { anchors.fill: parent; anchors.margins: -10 }
        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 6
            spacing: 18
            Text {
                id: body
                Layout.fillWidth: true
                text: dialog.text
                color: dialog.tint
                font.family: "Noto Sans Mono"
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    label: dialog.confirmLabel
                    key: "Enter"
                    color: dialog.tint
                    active: true
                    onClicked: dialog.confirmed()
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    label: dialog.keepLabel
                    key: "Esc"
                    color: "#FFB000"
                    onClicked: dialog.kept()
                }
            }
        }
    }
}
