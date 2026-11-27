import QtQuick
import QtQuick.Layouts

// The first start: a style clicked starts with it (every page gets its
// sound and the tempo), or the tutorial walks through a track. Shown once.
Rectangle {
    id: welcome
    color: "#000000"
    focus: visible
    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property var tints: ["#FF8A1F", "#FF4FD8", "#2EE6FF", "#FFB000", "#39FF88", "#A98BFF", "#FF4136", "#FFD23F"]
    signal tutorial()
    function startTutorial() {
        backend.endWelcome(-1)
        welcome.tutorial()
    }

    onVisibleChanged: if (visible) forceActiveFocus()
    // 1–8 start with that style, Enter the tutorial, Esc the studio's own sounds.
    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) startTutorial()
        else if (event.key === Qt.Key_Escape) backend.endWelcome(-1)
        else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_8 && event.key - Qt.Key_1 < backend.styleNames.length)
            backend.endWelcome(event.key - Qt.Key_1)
        else event.accepted = false
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
            text: qsTr("Pick a style to start from:")
            color: welcome.amber
            font.family: "Noto Sans Mono"
            font.pixelSize: 18
            wrapMode: Text.Wrap
        }

        GridLayout {
            id: grid
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
                    readonly property bool lit: cardMouse.containsMouse
                    readonly property var detail: backend.styleDetails[index]
                    Layout.fillWidth: true
                    Layout.preferredHeight: welcome.height < 700 ? 88 : 104
                    Rectangle {
                        anchors.fill: parent
                        color: card.lit ? card.tint : "transparent"
                        border.color: card.tint
                        border.width: card.lit ? 3 : 2
                    }
                    Text {
                        x: 12; y: 8
                        text: (card.index + 1) + " " + backend.styleNames[card.index]
                        color: card.lit ? "#0a0806" : card.tint
                        font.family: "Noto Sans Mono"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        y: 12
                        text: card.detail.bpm + " BPM"
                        color: card.lit ? "#0a0806" : card.tint
                        opacity: 0.8
                        font.family: "Noto Sans Mono"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    Text {
                        x: 12; y: 38
                        width: parent.width - 24
                        text: backend.styleWhats[card.index]
                        color: card.lit ? "#0a0806" : card.tint
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

        // The tutorial, as wide as the styles, lit as the way in.
        PanelButton {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.preferredHeight: 52
            label: qsTr("▶  Show me how, step by step")
            key: "Enter"
            color: welcome.green
            active: true
            labelSize: 18
            onClicked: welcome.startTutorial()
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Esc skips this and keeps the studio's own sounds.")
            color: welcome.amber
            opacity: 0.6
            font.family: "Noto Sans Mono"
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }
}
