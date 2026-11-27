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
    // The style chosen (and heard): Start begins with it.
    property int chosen: 0
    property bool heard: false
    readonly property int count: backend.styleNames.length
    function choose(i) {
        chosen = (i + count) % count
        heard = true
        backend.previewStyle(chosen)
    }

    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) backend.endWelcome(chosen)
        else if (event.key === Qt.Key_Escape) backend.endWelcome(-1)
        else if (event.key === Qt.Key_Left) choose(chosen - 1)
        else if (event.key === Qt.Key_Right) choose(chosen + 1)
        else if (event.key === Qt.Key_Up) choose(chosen - grid.columns)
        else if (event.key === Qt.Key_Down) choose(chosen + grid.columns)
        else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_8) choose(event.key - Qt.Key_1)
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
            text: qsTr("Make a track with your fingers. Touch a style to hear it, then start:")
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
                    readonly property bool lit: welcome.chosen === index
                    readonly property bool hot: lit || cardMouse.containsMouse
                    readonly property var detail: backend.styleDetails[index]
                    Layout.fillWidth: true
                    Layout.preferredHeight: welcome.height < 700 ? 100 : 122
                    Rectangle {
                        anchors.fill: parent
                        color: card.lit ? card.tint : cardMouse.containsMouse ? Qt.rgba(card.tint.r, card.tint.g, card.tint.b, 0.16) : "transparent"
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
                    Text {
                        x: 12
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        width: parent.width - 24
                        text: card.detail.sounds.join(" · ")
                        color: card.lit ? "#0a0806" : card.tint
                        opacity: 0.7
                        font.family: "Noto Sans Mono"
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // Touched again once heard: start with it.
                        onClicked: card.lit && welcome.heard ? backend.endWelcome(card.index) : welcome.choose(card.index)
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
                label: qsTr("▶  Start with %1").arg(backend.styleNames[welcome.chosen] || "")
                key: "Enter"
                color: welcome.green
                active: true
                beckon: welcome.heard
                onClicked: backend.endWelcome(welcome.chosen)
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
            text: qsTr("Every sound can be changed later with SOUND. Esc skips this and keeps the studio's own sounds.")
            color: welcome.amber
            opacity: 0.6
            font.family: "Noto Sans Mono"
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }
}
