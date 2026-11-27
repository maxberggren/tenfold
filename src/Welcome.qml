import QtQuick
import QtQuick.Layouts

// The first start: pick a style and every page gets its sound and the
// tempo, or just start playing. Shown once.
Rectangle {
    id: welcome
    color: "#000000"
    focus: visible
    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property var tints: ["#FF8A1F", "#FF4FD8", "#2EE6FF", "#FFB000", "#39FF88", "#A98BFF", "#FF4136", "#FFD23F"]
    signal tutorial()

    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            backend.endWelcome(-1)
            event.accepted = true
        } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_8) {
            backend.endWelcome(event.key - Qt.Key_1)
            event.accepted = true
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 1040)
        spacing: 18

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "TENFOLD"
            color: welcome.amber
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: Math.min(72, welcome.width / 12)
            font.weight: Font.Black
            font.letterSpacing: 10
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Make a track with your fingers. Pick a style to start from:")
            color: welcome.amber
            font.family: "Noto Sans Mono"
            font.pixelSize: 18
            wrapMode: Text.Wrap
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            columns: welcome.width < 760 ? 2 : 4
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: backend.styleNames.length
                delegate: Item {
                    id: card
                    required property int index
                    readonly property color tint: welcome.tints[index % welcome.tints.length]
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    Rectangle {
                        anchors.fill: parent
                        color: cardMouse.containsMouse ? card.tint : "transparent"
                        border.color: card.tint
                        border.width: 2
                    }
                    Text {
                        x: 12; y: 8
                        text: (card.index + 1) + " " + backend.styleNames[card.index]
                        color: cardMouse.containsMouse ? "#0a0806" : card.tint
                        font.family: "Noto Sans Mono"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        x: 12
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 10
                        width: parent.width - 24
                        text: backend.styleWhats[card.index]
                        color: cardMouse.containsMouse ? "#0a0806" : card.tint
                        opacity: 0.85
                        font.family: "Noto Sans Mono"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.endWelcome(card.index)
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 10
            spacing: 14
            PanelButton {
                Layout.preferredWidth: 300
                Layout.preferredHeight: 48
                label: qsTr("▶  Just let me play")
                key: "Enter"
                color: welcome.green
                active: true
                onClicked: backend.endWelcome(-1)
            }
            PanelButton {
                Layout.preferredWidth: 300
                Layout.preferredHeight: 48
                label: qsTr("Show me how, step by step")
                key: "Ctrl T"
                color: welcome.green
                onClicked: {
                    backend.endWelcome(-1)
                    welcome.tutorial()
                }
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Every sound can be changed later with SOUND, and more controls unfold as you go.")
            color: welcome.amber
            opacity: 0.6
            font.family: "Noto Sans Mono"
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }
}
