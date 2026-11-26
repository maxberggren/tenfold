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
        "1 – 8\t" + qsTr("the parts: drums, bass, chords, lead, arp, atmosphere, effects, vocal"),
        "0\t" + qsTr("the Song page: your loops become a track (sections, builds and drops)"),
        "9\t" + qsTr("the Visual page: everything playing as a picture, which your fingers reshape; Shift+Return there opens it in a window of its own, for a second screen"),
        "Tab\t" + qsTr("next page (Shift: previous)"),
        "Space\t" + qsTr("loop what you just played. With nothing else playing, press it where your phrase would start again: that sets the tempo. Over a playing loop, the bars you just played loop from the bar line. Over this page's own loop it layers on top, as one loop"),
        "Take the trackpad\t" + qsTr("the bright button beside the pad: the pointer stops and every finger plays"),
        "Esc\t" + qsTr("give the trackpad back; with it not taken, pause and play (as Ctrl P). First it closes a menu, this guide or the tutorial"),
        "Ctrl ↑ ↓\t" + qsTr("tempo (Shift: by five)"),
        "Ctrl ← →\t" + qsTr("key, by fifths"),
        "Ctrl [ ]\t" + qsTr("mode, darker or brighter"),
        "Ctrl M\t" + qsTr("metronome"),
        "Ctrl G\t" + qsTr("quantise, on to begin with: Space and overdubs tighten hits to the sixteenths. Off (Quantise beside the pad), a loop keeps your timing as you played it, laid back or pushed"),
        "Ctrl P\t" + qsTr("pause: every loop stops at once (Esc too, with the trackpad not taken). Again (or Play beside the pad): the same loops play on together from a downbeat, and the Song page's song from the bar it stopped in"),
        "Enter\t" + qsTr("play and pause: every loop stops at once, and again plays on from a downbeat (as Ctrl P). Shift Enter is what Return was on each page: hold, latch, keep a roll, drop; F10 is overdub"),
        "Ctrl ⌫\t" + qsTr("stop every clip at the next bar (Enter plays them again)"),
        "↑ ↓ after Space\t" + qsTr("for three seconds, the loop just made longer or shorter (1, 2, 4, 8, 16 bars); it stays once you stop. Loop shorter / Loop longer beside the pad do the same until you make another loop; on Drums, ↑ ↓ do too, at any time"),
        "Shift Space\t" + qsTr("a new loop of what you just played, in place of this part's playing loop"),
        "Ctrl Shift Z\t" + qsTr("bring back what Ctrl Z took back, on the same page"),
        "Ctrl Z\t" + qsTr("take back this page's newest loop change (a loop, a layer, a pass played into it), up to 16 back; on the Song page, the newest anywhere"),
        "Ctrl N, Ctrl N\t" + qsTr("start over: every loop cleared, every part unmuted and the mix set back, the next loop sets the tempo"),
        "Ctrl S\t" + qsTr("save the song: every loop, the mix, each page's sounds and the Song page, to pick up where you left off (Shift: save as)"),
        "Ctrl O\t" + qsTr("open a saved song in place of what plays now"),
        "Ctrl Shift O\t" + qsTr("pick up the song as it was when tenfold last quit"),
        "Ctrl R\t" + qsTr("record to ~/Music/tenfold"),
        "Ctrl Shift R\t" + qsTr("record the Visual page's picture with the sound, to an .mp4 beside the recording (needs ffmpeg; Shift+click Record does the same)"),
        "Ctrl T\t" + qsTr("the tutorial: a whole track, step by step, from a cleared studio (with loops made, press it twice; Ctrl Return next, Ctrl Shift Return back, Esc leaves)"),
        "Ctrl Q\t" + qsTr("quit")
    ]

    component Entry: RowLayout {
        property string line: ""
        property color tint: page.tint
        spacing: 16
        Text {
            // A narrow window: a narrower column of keys.
            Layout.preferredWidth: Math.min(300, page.width * 0.3)
            // Arrows and key symbols are the console's own drawings.
            textFormat: Text.StyledText
            text: backend.rich(line.split("\t")[0], tint, 16)
            color: tint
            font.family: "Noto Sans Mono"
            font.pixelSize: 16
            font.weight: Font.Bold
            font.capitalization: Font.AllUppercase
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            textFormat: Text.StyledText
            text: backend.rich(line.split("\t").slice(1).join(" "), tint, 16)
            color: tint
            font.family: "Noto Sans Mono"
            font.pixelSize: 16
            wrapMode: Text.Wrap
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: page.width < 1000 ? 16 : 40
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
                    line: backend.keys(modelData)
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
                    line: backend.keys(modelData)
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
