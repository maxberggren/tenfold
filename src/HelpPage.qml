import QtQuick
import QtQuick.Layouts

// The guide: what the fingers and keys do on the page that is showing, and
// the keys that work everywhere.
Rectangle {
    id: page
    property string title: ""
    property color tint: "#FFB000"
    property string pageHelp: ""
    color: "#000000"

    readonly property var pageLines: pageHelp.split("\n").filter(l => l.length > 0)
    readonly property var globalLines: [
        "1 – 7\t" + qsTr("the parts: drums, bass, chords, lead, arp, atmosphere, effects"),
        "0\t" + qsTr("arrange: every part's clips together"),
        "Tab\t" + qsTr("next page (Shift: previous)"),
        "Space\t" + qsTr("loop what you just played. With nothing else playing, press it where your phrase would start again: that sets the tempo. Over a playing loop, the bars you just played loop from the bar line. Over this page's own loop it layers on top, as one loop"),
        "Take pad / Esc\t" + qsTr("take the trackpad (the button) / give it back"),
        "Ctrl ↑ ↓\t" + qsTr("tempo (Shift: by five)"),
        "Ctrl ← →\t" + qsTr("key, by fifths"),
        "Ctrl [ ]\t" + qsTr("mode, darker or brighter"),
        "Ctrl M\t" + qsTr("metronome"),
        "Ctrl ⌫\t" + qsTr("stop every clip at the next bar"),
        "Shift Space\t" + qsTr("a new loop of what you just played, in place of this part's playing loop"),
        "Ctrl Z\t" + qsTr("take back the newest loop, on any page; what played before plays again"),
        "Ctrl N, Ctrl N\t" + qsTr("start over: every loop cleared, every part unmuted and the mix set back, the next loop sets the tempo"),
        "Ctrl R\t" + qsTr("record to ~/Music/tenfold"),
        "Ctrl T\t" + qsTr("the tutorial: a whole track, step by step, from a cleared studio (with loops made, press it twice; Ctrl Return next, Ctrl Shift Return back, Esc leaves)"),
        "Ctrl Q\t" + qsTr("quit")
    ]

    component Entry: RowLayout {
        property string line: ""
        property color tint: page.tint
        spacing: 16
        Text {
            Layout.preferredWidth: 300
            text: line.split("\t")[0]
            color: tint
            font.family: "Noto Sans Mono"
            font.pixelSize: 16
            font.weight: Font.Bold
            font.capitalization: Font.AllUppercase
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: line.split("\t").slice(1).join(" ")
            color: tint
            font.family: "Noto Sans Mono"
            font.pixelSize: 16
            wrapMode: Text.Wrap
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 40
        contentHeight: column.implicitHeight
        clip: true

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: page.tint
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: qsTr("Guide · %1").arg(page.title)
                    color: "#0a0806"
                    font.family: "Noto Sans Mono"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    font.capitalization: Font.AllUppercase
                }
            }
            Repeater {
                model: page.pageLines
                delegate: Entry {
                    required property string modelData
                    Layout.fillWidth: true
                    line: modelData
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.preferredHeight: 32
                color: "#FFB000"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: qsTr("Everywhere")
                    color: "#0a0806"
                    font.family: "Noto Sans Mono"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    font.capitalization: Font.AllUppercase
                }
            }
            Repeater {
                model: page.globalLines
                delegate: Entry {
                    required property string modelData
                    Layout.fillWidth: true
                    line: modelData
                    tint: "#FFB000"
                }
            }
            Text {
                Layout.topMargin: 12
                text: qsTr("Esc or ? closes the guide.")
                color: "#FFB000"
                opacity: 0.7
                font.family: "Noto Sans Mono"
                font.pixelSize: 14
            }
        }
    }
}
