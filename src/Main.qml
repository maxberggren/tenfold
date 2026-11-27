import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Tenfold

// The window is a console: phosphor outlines on black glass. Each part has
// its own phosphor, and its page fills the glass when its number is pressed;
// amber is the panel itself, green "ready", red recording and alarms.
ApplicationWindow {
    id: win
    width: 1440
    height: 940
    // A tiling window manager gives windows any size, these or not: the
    // window lays itself out for whatever it gets (see `stacked`).
    minimumWidth: 640
    minimumHeight: 440
    visible: true
    title: "tenfold"
    color: "#000000"

    Material.theme: Material.Dark
    Material.accent: amber
    Material.roundedScale: Material.NotRounded

    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property color cyan: "#2EE6FF"
    readonly property color red: "#FF4136"
    readonly property string wide: "JetBrainsMono Nerd Font"
    readonly property string mono: "Noto Sans Mono"

    property bool helpVisible: false
    // A screenshot's: the notice stays up (tenfold screenshot --notice).
    property bool pinNotice: false
    property bool exportVisible: false
    // The sound browser over the pad (SOUND, or S... the button only: S is a key on pages).
    property bool soundsVisible: false
    onSoundsVisibleChanged: if (!soundsVisible) screen.forceActiveFocus()
    // How much shows: 0 Play, 1 Shape, 2 Studio (Session::level).
    readonly property int lv: backend.uiLevel
    // At level 0 the tabs show the first parts and the Song; MORE opens the rest.
    property bool allPages: false
    function pageShown(i) {
        return lv >= 2 || allPages || i < 4 || i === backend.arrangePage || i === backend.page
    }
    readonly property int shownPages: {
        let n = 0
        for (let i = 0; i < backend.pageCount; ++i)
            n += pageShown(i) ? 1 : 0
        return n
    }
    readonly property bool moreTab: lv < 2 && !allPages
    // The dialog has the keyboard while it shows (its prefix field may take
    // it), and the pages get it back when it goes.
    onExportVisibleChanged: exportVisible ? exportDialog.forceActiveFocus() : screen.forceActiveFocus()
    readonly property bool playing: backend.padState === Backend.Playing
    readonly property color pageColor: backend.pageColors[backend.page]

    // The page fills what the window has. The panel of buttons goes beside
    // the pad in a column, beside it in two columns (a wide, short window),
    // or under it (a narrow one): whichever keeps all the panel in sight
    // with the pad nearest the trackpad's shape, 1.6 wide to 1 high, and
    // wide enough for the controls under it.
    readonly property int margin: width < 1000 || height < 700 ? 12 : 20
    readonly property int sideWidth: backend.tutorialActive ? 360 : 300
    readonly property int wideWidth: 300 + 16 + sideWidth
    // The height the panel needs in each, its buttons at their smallest.
    readonly property int sideHeight: backend.tutorialActive ? 400 : lv === 0 ? 470 : lv === 1 ? 560 : 600
    readonly property int wideHeight: backend.tutorialActive ? 240 : 300
    readonly property int stackHeight: backend.tutorialActive ? 320 : 260
    // The pad's item and the panel get this much of the window's height.
    readonly property real room: height - 2 * margin - header.height - 20 - 28
    readonly property int arrangement: {
        const w = width - 2 * margin, h = room
        // How far the pad (Session::padRect: 60 px of frame across, 115 down)
        // is from the trackpad's shape; much more when the panel won't fit,
        // and when the pad is too narrow for the rows of controls under it.
        function cost(itemW, itemH, fits) {
            const padW = itemW - 60, padH = itemH - 115
            if (padH < 100)
                return 99
            return Math.abs(Math.log(Math.max(20, padW) / padH / 1.6)) + (fits ? 0 : 1) + (padW < 480 ? 2 : 0)
        }
        const costs = [cost(w - 16 - sideWidth, h, h >= sideHeight),
                       cost(w - 16 - wideWidth, h, h >= wideHeight) + 0.15,
                       cost(w, h - 10 - stackHeight, h - 10 - stackHeight - 115 >= 200) + 0.1]
        return costs.indexOf(Math.min(costs[0], costs[1], costs[2]))
    }
    readonly property bool stacked: arrangement === 2
    // Readouts on fewer lines, where room is short.
    readonly property bool compact: arrangement !== 0 || room < 680
    // The transport and the readouts on fewer lines: where compact, and
    // also while the pad isn't taken and its big button and note need the
    // room, so the column doesn't scroll at a common size.
    // (Below Studio the pad's button is small whatever its state: the
    // readouts keep their size, so taking the pad moves nothing.)
    readonly property bool readoutsCompact: compact || (!playing && room < 800 && lv >= 2)
    // A column too short for it all scrolls: the buttons first, then.
    readonly property bool buttonsFirst: arrangement === 0 && room < sideHeight && !backend.tutorialActive

    // The pad stays taken while any of tenfold's windows is in front (the
    // second window of the visualizer too), and is given back when none is.
    Connections {
        target: Qt.application
        function onStateChanged() { backend.windowActive(Qt.application.state === Qt.ApplicationActive) }
    }

    function quit() {
        if (backend.recording)
            quitDialog.open()
        else
            Qt.quit()
    }
    onClosing: close => {
        if (backend.recording) {
            close.accepted = false
            quitDialog.open()
        }
    }
    // The key a page answers to: 1–8 for the first eight parts, Shift+1 for
    // the Sampler, 0 for the Song page, 9 for the visualizer.
    function pageKey(i) { return backend.pageKeys[i] }
    // Esc closes what is open first: the guide, a right-click menu, the
    // tutorial; then it gives the trackpad back while it's taken (or being
    // taken), and with nothing of that to escape it pauses and plays, as
    // Ctrl+P (Session::escape).
    function back() {
        if (win.soundsVisible)
            win.soundsVisible = false
        else if (win.exportVisible)
            win.exportVisible = false // an export goes on: only its Cancel stops it
        else if (win.helpVisible)
            win.helpVisible = false
        else
            backend.escape()
    }
    // Record, and Shift+Record: the visualizer's picture with the sound.
    function record(modifiers) {
        if (modifiers & Qt.ShiftModifier)
            backend.toggleVideoRecording()
        else
            backend.toggleRecording()
    }
    function barsText(beats) {
        if (beats % 4 !== 0)
            return beats === 1 ? qsTr("→ 1 beat") : qsTr("→ %1 beats").arg(beats)
        return beats === 4 ? qsTr("→ 1 bar") : qsTr("→ %1 bars").arg(beats / 4)
    }

    // This window's own: the visualizer's window has its Esc (leave full
    // screen) and F11.
    Shortcut { sequence: "Esc"; context: Qt.WindowShortcut; onActivated: win.back() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: backend.toggleRecording() }
    Shortcut { sequence: "Ctrl+Shift+R"; context: Qt.ApplicationShortcut; onActivated: backend.toggleVideoRecording() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.saveSong() }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; onActivated: backend.saveSongAs() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: backend.openSong() }
    Shortcut { sequence: "Ctrl+Shift+O"; context: Qt.ApplicationShortcut; onActivated: backend.reopenLastSong() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; onActivated: win.exportVisible = true }
    // A link to a song (or a sound file's path) on the clipboard goes to the Sampler.
    Shortcut { sequence: StandardKey.Paste; context: Qt.ApplicationShortcut; onActivated: backend.paste() }
    Shortcut {
        sequence: "F11"; context: Qt.WindowShortcut
        onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Item {
        id: screen
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // Every other key goes to the session: page numbers, tempo and key on
        // Ctrl, and whatever the showing page plays from the keyboard.
        Keys.onPressed: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, true, event.isAutoRepeat, event.text)
        }
        Keys.onReleased: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, false, event.isAutoRepeat, event.text)
        }

        // A sound file dragged in from the file manager, or a link dragged
        // from a browser: the Sampler takes it, and its page is shown.
        DropArea {
            id: dropArea
            anchors.fill: parent
            onDropped: drop => {
                if (drop.hasUrls && drop.urls.length > 0)
                    backend.drop(drop.urls[0].toString())
                else if (drop.hasText)
                    backend.drop(drop.text)
                drop.accept()
            }
        }
        Rectangle {
            anchors.fill: parent
            z: 50
            visible: dropArea.containsDrag
            color: Qt.rgba(0, 0, 0, 0.72)
            border.color: win.pageColor
            border.width: 2
            Text {
                anchors.centerIn: parent
                text: qsTr("DROP TO SAMPLE")
                color: win.pageColor
                font.family: theme.fontFamily
                font.pixelSize: 34
                font.weight: Font.Black
                font.letterSpacing: 4
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: win.margin
            spacing: 10

            // Wordmark and the pages, one tab each, numbered as the keys are:
            // one row where they fit, the wordmark too if it does; in a
            // narrow window, two rows (or more) beside it.
            Item {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: rows * tabHeight + (rows - 1) * 6
                readonly property int count: win.shownPages + (win.moreTab ? 1 : 0)
                readonly property int minTab: 88
                readonly property int gap: 10
                readonly property real markWidth: wordmark.implicitWidth + 18
                function perRow(room) { return Math.max(1, Math.floor((room + gap) / (minTab + gap))) }
                readonly property bool oneRowMarked: perRow(width - markWidth) >= count
                readonly property bool oneRow: oneRowMarked || perRow(width) >= count
                readonly property bool marked: oneRowMarked || (!oneRow && width >= 680)
                readonly property int rows: oneRow ? 1 : Math.ceil(count / perRow(width - (marked ? markWidth : 0)))
                // A short window: two rows of names only.
                readonly property int tabHeight: rows === 1 ? 58 : win.height < 640 ? 34 : 46

                Text {
                    id: wordmark
                    visible: header.marked
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "TENFOLD"
                    color: win.amber
                    font.family: win.wide
                    font.pixelSize: 30
                    font.weight: Font.Black
                    font.letterSpacing: 5
                }
                GridLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: header.marked ? header.markWidth : 0
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    columns: Math.ceil(header.count / header.rows)
                    columnSpacing: header.gap
                    rowSpacing: 6

                    Repeater {
                        model: backend.pageCount
                        delegate: Item {
                            id: tab
                            required property int index
                            readonly property color tint: backend.pageColors[index]
                            readonly property bool current: backend.page === index
                            readonly property bool sounding: backend.pagesPlaying[index] === true
                            readonly property string silenced: backend.pagesSilenced[index] || ""
                            // The tutorial's next page, while the player isn't on it.
                            readonly property bool called: (backend.tutorialActive ? backend.tutorialPage === index
                                                                                   : win.lv < 2 && backend.suggestedPage === index)
                                                           && !current
                            visible: win.pageShown(index)
                            Layout.fillWidth: true
                            Layout.preferredWidth: header.minTab
                            Layout.preferredHeight: header.tabHeight
                            Rectangle {
                                anchors.fill: parent
                                color: tab.current ? tab.tint : "transparent"
                                border.color: tab.tint
                                border.width: 2
                            }
                            // The tab to go to next: filled softly in its own colour.
                            Rectangle {
                                visible: tab.called
                                anchors.fill: parent
                                anchors.margins: 2
                                color: Qt.rgba(tab.tint.r, tab.tint.g, tab.tint.b, 0.3)
                            }
                            Text {
                                x: 8
                                y: tab.height >= 44 ? 5 : (tab.height - height) / 2
                                width: parent.width - 16
                                // A key with a symbol in it (⇧1) is drawn with the
                                // console's own line art, as the buttons' keys are;
                                // its longer name a little smaller, so it fits.
                                readonly property string label: win.pageKey(tab.index) + " " + backend.pageNames[tab.index].toUpperCase()
                                readonly property bool iconed: backend.hasIcons(label)
                                textFormat: iconed ? Text.StyledText : Text.PlainText
                                text: iconed ? backend.rich(label, color, font.pixelSize) : label
                                color: tab.current ? "#0a0806" : tab.tint
                                font.family: win.mono
                                font.pixelSize: iconed ? (parent.width >= 124 ? 15 : parent.width >= 96 ? 13 : 11) : 15
                                fontSizeMode: iconed ? Text.FixedSize : Text.HorizontalFit
                                minimumPixelSize: 11
                                font.weight: Font.Bold
                                font.capitalization: Font.AllUppercase
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: tab.height >= 44
                                x: 8
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 6
                                width: parent.width - 30
                                // A newcomer's tabs say what each part is, in a word or two.
                                readonly property var plain: ({ 0: qsTr("the beat"), 1: qsTr("the low end"), 2: qsTr("the harmony"),
                                                                3: qsTr("the melody"), 4: qsTr("running notes"), 5: qsTr("pads and air"),
                                                                6: qsTr("builds and drops") })
                                text: tab.silenced !== "" ? "■ " + tab.silenced
                                      : win.lv < 2 && plain[tab.index] !== undefined ? plain[tab.index]
                                      : win.lv < 2 && tab.index === backend.arrangePage ? qsTr("make a track")
                                      : backend.pageBlurbs[tab.index]
                                color: tab.current ? "#0a0806" : tab.tint
                                opacity: 0.75
                                font.family: win.mono
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                                elide: Text.ElideRight
                            }
                            // Lit while one of the part's clips is playing.
                            Rectangle {
                                visible: tab.sounding
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 7
                                width: 10
                                height: 10
                                color: tab.current ? "#0a0806" : tab.tint
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.page = tab.index
                            }
                        }
                    }
                    // Level 0: the other parts, one click away.
                    Item {
                        visible: win.moreTab
                        Layout.fillWidth: true
                        Layout.preferredWidth: header.minTab
                        Layout.preferredHeight: header.tabHeight
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: win.amber
                            border.width: 2
                            opacity: 0.6
                            Rectangle { anchors.fill: parent; anchors.margins: 4; color: "transparent"; border.color: win.amber; border.width: 1; opacity: 0.5 }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("+ %n more", "", backend.pageCount - win.shownPages)
                            color: win.amber
                            font.family: win.mono
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.capitalization: Font.AllUppercase
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.allPages = true }
                    }
                }
            }

            // The pad, and the panel beside it or under it.
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: win.stacked ? 1 : 2
                columnSpacing: 16
                rowSpacing: 10

                Surface {
                    id: surface
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    app: backend

                    // The Visual page's picture, in the pad's place, under
                    // what the page draws (its frame, the fingers, its band).
                    Visualizer {
                        z: -1
                        x: surface.pad.x
                        y: surface.pad.y
                        width: surface.pad.width
                        height: surface.pad.height
                        visible: visual.showing
                        crt: visual.crt ? 0.45 : 0
                        fingers: true
                    }
                }

                // The panel: the clock and key every part shares, what's ready,
                // the readouts, and the buttons. Beside the pad it is one column
                // or two; under it, two, the buttons in as many columns as fit.
                // Where the window is too short for all of it, it scrolls
                // rather than lose any of it off the edge.
                Flickable {
                    id: panelView
                    Layout.fillWidth: win.stacked
                    Layout.fillHeight: !win.stacked
                    Layout.preferredWidth: win.stacked ? -1 : win.arrangement === 1 ? win.wideWidth : win.sideWidth
                    Layout.maximumWidth: win.stacked ? Number.POSITIVE_INFINITY : Layout.preferredWidth
                    // Under the pad, the tutorial gets room to be read.
                    Layout.preferredHeight: win.stacked ? Math.max(panel.Layout.minimumHeight, backend.tutorialActive ? win.stackHeight : 0) : -1
                    readonly property bool scrolls: panel.Layout.minimumHeight > height + 1
                    contentWidth: width
                    contentHeight: panel.height
                    clip: scrolls
                    interactive: scrolls
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { id: panelBar; policy: panelView.scrolls ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

                    GridLayout {
                        id: panel
                        // Clear of the scroll bar, whatever its style makes its width.
                        width: panelView.width - (panelView.scrolls ? panelBar.width + 4 : 0)
                        height: Math.max(panelView.height, Layout.minimumHeight)
                        columns: win.arrangement === 0 ? 1 : 2
                        columnSpacing: 16
                        rowSpacing: 12

                        // Where each piece goes. In a column: the clock, the
                        // tutorial or the readouts, the buttons (or, too short
                        // for it all, the buttons straight under the clock).
                        // In two: the clock and the readouts on the left, the
                        // buttons on the right; with the tutorial, the clock
                        // and the buttons on the left, the tutorial on the right.
                        readonly property bool twoColumns: win.arrangement !== 0
                        readonly property bool tutorial: backend.tutorialActive

                        PanelBox {
                            Layout.row: 0
                            Layout.column: 0
                            Layout.fillWidth: !panel.twoColumns
                            Layout.preferredWidth: panel.twoColumns ? 300 : -1
                            Layout.alignment: Qt.AlignTop
                            // Room for the three level chips beside the readouts.
                            Layout.preferredHeight: panel.tutorial ? (win.readoutsCompact ? 96 : 108) : win.readoutsCompact ? 112 : 128
                            title: qsTr("Transport")
                            color: win.amber
                            // How much shows: Play, Shape, Studio, always in reach
                            // here at the top. The keys work at every level.
                            Column {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                width: 92
                                spacing: 4
                                visible: !backend.tutorialActive
                                Repeater {
                                    // Play and Studio: the levels 0 and 2.
                                    model: [qsTr("Play"), qsTr("Studio")]
                                    delegate: Rectangle {
                                        id: levelChip
                                        required property int index
                                        required property string modelData
                                        width: parent.width
                                        height: win.readoutsCompact ? 26 : 32
                                        readonly property int level: index === 0 ? 0 : 2
                                        color: win.lv === level ? win.amber : levelMouse.containsMouse ? Qt.rgba(1, 0.69, 0, 0.18) : "transparent"
                                        border.color: win.amber
                                        border.width: 1
                                        opacity: win.lv === level || levelMouse.containsMouse ? 1 : 0.7
                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData
                                            color: win.lv === levelChip.level ? "#0a0806" : win.amber
                                            font.family: win.mono
                                            font.pixelSize: 12
                                            font.weight: Font.Bold
                                            font.letterSpacing: 1
                                            font.capitalization: Font.AllUppercase
                                        }
                                        MouseArea {
                                            id: levelMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: backend.uiLevel = levelChip.level
                                        }
                                    }
                                }
                            }
                            Column {
                                spacing: 2
                                Text {
                                    text: qsTr("%1 BPM").arg(backend.bpm.toFixed(0))
                                    color: win.amber
                                    font.family: win.mono
                                    font.pixelSize: 26
                                    font.weight: Font.Bold
                                }
                                // Compact: the chord on the key's line.
                                Text {
                                    // Clear of the level chips on the right.
                                    width: parent.parent.width - (backend.tutorialActive ? 0 : 100)
                                    elide: Text.ElideRight
                                    text: qsTr("Key %1 %2").arg(backend.key).arg(backend.mode)
                                          + (win.readoutsCompact && backend.tutorialActive ? "  ·  " + qsTr("Chord %1").arg(backend.chord) : "")
                                    color: win.cyan
                                    font.family: win.mono
                                    font.pixelSize: win.readoutsCompact ? 15 : 16
                                    font.weight: Font.DemiBold
                                    font.capitalization: Font.AllUppercase
                                }
                                Text {
                                    // Its own line beside the level chips; on the key's line in the tutorial.
                                    visible: !win.readoutsCompact || !backend.tutorialActive
                                    text: qsTr("Chord %1").arg(backend.chord)
                                    color: win.cyan
                                    font.family: win.mono
                                    font.pixelSize: 14
                                    font.capitalization: Font.AllUppercase
                                }
                            }
                        }

                        // What's ready, and the readouts.
                        ColumnLayout {
                            Layout.row: panel.twoColumns ? 1 : win.buttonsFirst ? 2 : 1
                            Layout.column: 0
                            Layout.fillWidth: !panel.twoColumns
                            Layout.preferredWidth: panel.twoColumns ? 300 : -1
                            Layout.alignment: Qt.AlignTop
                            visible: !panel.tutorial
                            spacing: win.readoutsCompact ? 10 : 12

                            // Below the Studio level: what to do next, in place of the readouts.
                            PanelBox {
                                visible: win.lv < 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: nextText.implicitHeight + 34
                                title: qsTr("Next")
                                color: win.pageColor
                                Text {
                                    id: nextText
                                    width: parent.width
                                    text: win.soundsVisible ? qsTr("Click a sound to play with it. Esc keeps the one you had.") : backend.nextStep
                                    color: win.pageColor
                                    font.family: win.mono
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.Wrap
                                }
                            }

                            Column {
                                visible: win.lv >= 2
                                Layout.fillWidth: true
                                spacing: win.readoutsCompact ? 4 : 6
                                StatusLamp { label: qsTr("Trackpad grabbed"); on: win.playing; color: win.green }
                                StatusLamp { label: qsTr("Audio running"); on: backend.audioOk; color: win.green }
                                StatusLamp { label: qsTr("Metronome"); on: backend.metronome; color: win.amber }
                                StatusLamp { label: qsTr("Recording"); on: backend.recording; color: win.red }
                            }

                            // The readouts: a lit heading over them, or (compact)
                            // at the start of their line.
                            Item {
                                visible: win.lv >= 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: win.readoutsCompact ? 32 : 60
                                Rectangle {
                                    id: dspHead
                                    width: win.readoutsCompact ? dspTitle.implicitWidth + 16 : parent.width
                                    height: win.readoutsCompact ? parent.height : 22
                                    color: win.green
                                    Text {
                                        id: dspTitle
                                        anchors.centerIn: parent
                                        text: win.readoutsCompact ? qsTr("DSP") : qsTr("DSP values")
                                        color: "#0a0806"
                                        font.family: win.mono
                                        font.pixelSize: 14
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1.5
                                        font.capitalization: Font.AllUppercase
                                    }
                                }
                                Rectangle {
                                    x: win.readoutsCompact ? dspHead.width : 0
                                    y: win.readoutsCompact ? 0 : dspHead.height
                                    width: parent.width - x
                                    height: parent.height - y
                                    color: "transparent"
                                    border.color: win.green
                                    border.width: 2
                                    Text {
                                        anchors.centerIn: parent
                                        text: (backend.cpu * 100).toFixed(0) + " %   "
                                              + (backend.latencyMs > 0 ? backend.latencyMs.toFixed(1) + " MS" : "— MS")
                                              + "   " + qsTr("%n voices", "", backend.voices)
                                        color: win.green
                                        font.family: win.mono
                                        font.pixelSize: win.readoutsCompact ? 15 : 17
                                        font.weight: Font.DemiBold
                                        font.capitalization: Font.AllUppercase
                                    }
                                }
                            }
                        }

                        // Below Studio, the room left over goes under everything.
                        Item {
                            Layout.row: 3
                            Layout.column: 0
                            Layout.fillHeight: true
                            Layout.preferredHeight: 0
                            visible: win.lv < 2 && !panel.tutorial
                        }

                        // In a column, under the clock; in two, to the right of
                        // the clock and the buttons, all their height.
                        TutorialPanel {
                            Layout.row: panel.twoColumns ? 0 : 1
                            Layout.column: panel.twoColumns ? 1 : 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.rowSpan: win.arrangement === 0 ? 1 : 2
                            Layout.minimumHeight: win.compact ? 160 : 240
                            visible: backend.tutorialActive
                            tint: backend.pageColors[backend.tutorialPage]
                            onPage: backend.page === backend.tutorialPage
                            pageKey: win.pageKey(backend.tutorialPage)
                            pageName: backend.pageNames[backend.tutorialPage]
                            onBack: backend.tutorialBack()
                            onNext: backend.tutorialNext()
                            onLeave: backend.toggleTutorial()
                        }

                        GridLayout {
                            id: buttons
                            // In two columns with the tutorial, under the clock.
                            readonly property bool underClock: panel.twoColumns && panel.tutorial
                            Layout.row: panel.twoColumns ? (underClock ? 1 : 0) : win.buttonsFirst ? 1 : 2
                            Layout.column: panel.twoColumns && !underClock ? 1 : 0
                            Layout.rowSpan: panel.twoColumns && !underClock ? 2 : 1
                            Layout.fillWidth: !underClock
                            Layout.preferredWidth: underClock ? 300 : -1
                            // Below Studio the panel packs to the top, LOOP near the NEXT box.
                            Layout.fillHeight: (!panel.tutorial || panel.twoColumns) && win.lv >= 2
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: panel.twoColumns || win.buttonsFirst ? 0 : 6
                            // Two columns beside the pad; under it, as many as
                            // there's room for, each wide enough for its label.
                            columns: win.stacked && !backend.tutorialActive
                                     ? Math.max(2, Math.min(8, Math.floor((width + columnSpacing) / (140 + columnSpacing))))
                                     : 2
                            columnSpacing: 8
                            rowSpacing: backend.tutorialActive ? 5 : win.compact ? 6 : 8
                            property int bh: backend.tutorialActive ? 34 : 40
                                // The only way to take the pad: the whole row at
                                // the top, and while the pad isn't taken, big,
                                // lit and breathing, so it can't be missed.
                                PanelButton {
                                    readonly property bool waiting: backend.padState === Backend.Starting
                                    readonly property bool failed: backend.padState === Backend.Failed
                                    Layout.columnSpan: buttons.columns
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    // Level 0: the mouse and the keys play too, so the
                                    // pad's button stands back for LOOP.
                                    readonly property bool modest: win.lv === 0 && !backend.tutorialActive
                                    // At Play one size whatever its state, so taking the pad
                                    // doesn't move the panel under the hand.
                                    Layout.minimumHeight: modest ? 48 : win.playing ? 30 : backend.tutorialActive || win.compact ? 52 : 84
                                    Layout.maximumHeight: modest ? 48 : win.playing ? 60 : 112
                                    Layout.preferredHeight: modest ? 48 : win.playing ? buttons.bh : backend.tutorialActive || win.compact ? 60 : 96
                                    label: win.playing ? (modest ? qsTr("Release the trackpad") : qsTr("Release pad"))
                                         : waiting ? (backend.padNeedsPassword ? qsTr("Waiting for your password…") : qsTr("Taking the trackpad…"))
                                         : failed ? qsTr("▶  Retry the trackpad")
                                         : modest ? qsTr("Play with the trackpad")
                                         : qsTr("▶  Take the trackpad")
                                    // As big as fits on one line (a letter is about 0.65 of
                                    // the size across).
                                    labelSize: win.playing || modest ? 16 : Math.max(12, Math.min(backend.tutorialActive ? 19 : 22, Math.floor((width - 24) / (0.65 * label.length))))
                                    // Little glow where there's little room round it.
                                    glowSpread: win.compact ? 4 : 11
                                    note: win.playing ? ""
                                        : failed ? backend.error
                                        : waiting ? qsTr("Click to call it off.")
                                        : modest ? qsTr("Your fingers become the pads. Esc gives it back.")
                                        : qsTr("The pointer stops and every finger plays. Esc gives it back.")
                                          + (backend.padNeedsPassword ? " " + qsTr("Needs your password, once each time tenfold starts.") : "")
                                    key: win.playing || waiting ? "Esc" : ""
                                    // Lit green from the click until it is given back: taking
                                    // it changes the words, never the colour.
                                    color: failed ? win.red : win.green
                                    active: win.playing || waiting
                                    beckon: !win.playing && !waiting && !modest
                                    hint: backend.tutorialActive && backend.tutorialButton === "pad" && !win.playing
                                    onClicked: backend.togglePlay()
                                }
                                // LOOP: Space, as a button that says what it will do now
                                // (Session::LoopState: 1 nothing played, 2 loop it,
                                // 3 layer on the loop playing, 4 waiting for the bar).
                                PanelButton {
                                    id: loopButton
                                    readonly property int st: backend.loopState
                                    visible: st > 0 && !backend.tutorialActive
                                    Layout.columnSpan: buttons.columns
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    // At Play the hero: what the window has left over, within reason.
                                    Layout.minimumHeight: win.lv === 0 && win.room >= 560 ? 64 : 44; Layout.maximumHeight: win.lv === 0 ? 260 : 120
                                    Layout.preferredHeight: win.lv === 0 ? Math.max(win.room < 560 ? 44 : 64, Math.min(260, win.room - 400 - (yourLoops.visible ? yourLoops.Layout.preferredHeight + 6 : 0))) : 64
                                    labelSize: win.lv === 0 ? 26 : 20
                                    color: st === 4 ? win.red : win.pageColor
                                    // While the browser shows, Space keeps a sound there: LOOP steps back.
                                    opacity: win.soundsVisible ? 0.3 : st === 1 ? 0.55 : 1
                                    label: st === 4 ? qsTr("Looping on the bar…")
                                         : st === 3 ? qsTr("+  Add a layer")
                                         : qsTr("Loop it")
                                    note: st === 4 ? qsTr("Keep playing. Click to call it off.")
                                        : st === 3 ? qsTr("Play over the loop, then press.")
                                        : st === 2 ? qsTr("Keeps what you just played going round.")
                                        : qsTr("Play something first, then press.")
                                    key: win.soundsVisible ? "" : "Space"
                                    active: st === 4
                                    beckon: st === 2 && !win.soundsVisible
                                    glowSpread: win.compact ? 4 : 8
                                    onClicked: modifiers => backend.pressLoop((modifiers & Qt.ShiftModifier) !== 0)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    label: qsTr("Undo"); key: "Ctrl Z"; color: win.amber
                                    hint: backend.tutorialActive && backend.tutorialButton === "undo"
                                    onClicked: backend.undoTake()
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 2 || backend.recording
                                    label: backend.recording
                                           ? (backend.recordingVideo ? qsTr("Video %1") : qsTr("Rec %1"))
                                             .arg(new Date(backend.recordSeconds * 1000).toISOString().substr(14, 5))
                                           : qsTr("Record")
                                    // Shift+click (Ctrl+Shift+R): the picture too.
                                    key: backend.recording ? "Ctrl R" : "Ctrl R · ⇧ video"; color: win.red; active: backend.recording
                                    hint: backend.tutorialActive && backend.tutorialButton === "record"
                                    onClicked: modifiers => win.record(modifiers)
                                }
                                PanelButton {
                                    // The transport. Every loop stops at once, remembered;
                                    // again, they play on together from a downbeat.
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    label: backend.loopsPaused ? qsTr("▶ Play") : qsTr("❚❚ Pause")
                                    // Enter always; Esc too while Esc would pause (no pad
                                    // taken or being taken, no tutorial to leave): the
                                    // shorter key, then.
                                    key: "Enter"; color: win.green; active: backend.loopsPaused
                                    onClicked: backend.togglePause()
                                }
                                // Below Studio: what the player has made so far, a line a
                                // part, each started or stopped at the bar with a click.
                                PanelBox {
                                    id: yourLoops
                                    readonly property var parts: backend.partLoops
                                    visible: win.lv < 2 && !backend.tutorialActive && parts.length > 0
                                    Layout.columnSpan: buttons.columns
                                    Layout.fillWidth: true
                                    Layout.topMargin: 6
                                    readonly property int rowH: win.room < 560 ? 22 : 26
                                    // A short window lists two, and says how many more, so LOOP keeps its room.
                                    readonly property int shown: win.room < 560 ? Math.min(2, parts.length) : parts.length
                                    readonly property int hidden: parts.length - shown
                                    Layout.preferredHeight: 34 + shown * (rowH + 4) + (hidden > 0 ? 18 : 0)
                                    title: qsTr("Your loops")
                                    color: win.amber
                                    Column {
                                        width: parent.width
                                        spacing: 4
                                        Repeater {
                                            model: yourLoops.parts.slice(0, yourLoops.shown)
                                            delegate: Rectangle {
                                                id: loopRow
                                                required property var modelData
                                                readonly property color tint: backend.pageColors[modelData.page]
                                                readonly property bool on: modelData.playing
                                                width: parent.width
                                                height: yourLoops.rowH
                                                color: rowMouse.containsMouse ? Qt.rgba(tint.r, tint.g, tint.b, 0.18) : "transparent"
                                                border.color: tint
                                                border.width: 1
                                                // Playing: lit square; stopped: an outline; waiting for the bar: blinking.
                                                Rectangle {
                                                    id: lamp
                                                    x: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: 10; height: 10
                                                    color: loopRow.on ? loopRow.tint : "transparent"
                                                    border.color: loopRow.tint
                                                    border.width: 1.5
                                                    SequentialAnimation on opacity {
                                                        running: loopRow.modelData.queued
                                                        loops: Animation.Infinite
                                                        NumberAnimation { to: 0.2; duration: 250 }
                                                        NumberAnimation { to: 1; duration: 250 }
                                                    }
                                                }
                                                Text {
                                                    x: 26
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: backend.pageNames[loopRow.modelData.page]
                                                          + (loopRow.modelData.loops > 1 ? "  ×" + loopRow.modelData.loops : "")
                                                    color: loopRow.tint
                                                    font.family: win.mono
                                                    font.pixelSize: 13
                                                    font.weight: Font.Bold
                                                    font.capitalization: Font.AllUppercase
                                                }
                                                Text {
                                                    anchors.right: parent.right
                                                    anchors.rightMargin: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    readonly property int bars: Math.round(loopRow.modelData.beats / 4)
                                                    text: (loopRow.modelData.beats < 4
                                                           ? (loopRow.modelData.beats === 1 ? qsTr("1 beat") : qsTr("%1 beats").arg(loopRow.modelData.beats))
                                                           : (bars === 1 ? qsTr("1 bar") : qsTr("%1 bars").arg(bars)))
                                                          + "   " + (rowMouse.containsMouse ? (loopRow.on ? qsTr("stop") : qsTr("play"))
                                                                                          : loopRow.on ? qsTr("playing") : qsTr("stopped"))
                                                    color: loopRow.tint
                                                    opacity: 0.8
                                                    font.family: win.mono
                                                    font.pixelSize: 12
                                                    font.capitalization: Font.AllUppercase
                                                }
                                                MouseArea {
                                                    id: rowMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: backend.toggleLoop(loopRow.modelData.page)
                                                }
                                            }
                                        }
                                        Text {
                                            visible: yourLoops.hidden > 0
                                            text: yourLoops.hidden === 1 ? qsTr("+1 more part") : qsTr("+%1 more parts").arg(yourLoops.hidden)
                                            color: win.amber
                                            opacity: 0.7
                                            font.family: win.mono
                                            font.pixelSize: 12
                                            font.capitalization: Font.AllUppercase
                                        }
                                    }
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the keys still work; the tutorial needs the room
                                    label: qsTr("Tempo −"); key: "Ctrl ↓"; color: win.amber
                                    onClicked: backend.shiftTempo(-1)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the keys still work; the tutorial needs the room
                                    label: qsTr("Tempo +"); key: "Ctrl ↑"; color: win.amber
                                    onClicked: backend.shiftTempo(1)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the keys still work; the tutorial needs the room
                                    label: qsTr("Key ◀"); key: "Ctrl ←"; color: win.cyan
                                    onClicked: backend.shiftRoot(-1)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the keys still work; the tutorial needs the room
                                    label: qsTr("Key ▶"); key: "Ctrl →"; color: win.cyan
                                    onClicked: backend.shiftRoot(1)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the keys still work; the tutorial needs the room
                                    label: qsTr("Click"); key: "Ctrl M"; color: win.amber; active: backend.metronome
                                    onClicked: backend.toggleMetronome()
                                }
                                // On, Space and overdubs tighten the playing to
                                // the grid; off, loops keep it as it was heard.
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && !backend.tutorialActive // the key still works; the tutorial needs the room
                                    label: qsTr("Quantise"); key: "Ctrl G"; color: win.amber; active: backend.quantise
                                    onClicked: backend.toggleQuantise()
                                }
                                PanelButton {
                                    // Beside Export, so Open and Save share the next row
                                    // (with the tutorial, Export hides and it sits beside Undo).
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 2
                                    label: qsTr("Guide"); key: "?"; color: win.cyan; active: win.helpVisible
                                    onClicked: win.helpVisible = !win.helpVisible
                                }
                                // The song's file: save, open, export, start over, in one menu.
                                PanelButton {
                                    id: fileButton
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 2 && !backend.tutorialActive // the keys still work
                                    label: backend.exporting ? qsTr("Export %1%").arg(Math.round(backend.exportProgress * 100)) : qsTr("Song file ▾")
                                    key: "Ctrl S · O · E"; color: win.cyan; active: fileMenu.visible || backend.exporting
                                    onClicked: fileMenu.popup(fileButton, 0, fileButton.height)
                                    Menu {
                                        id: fileMenu
                                        MenuItem { text: qsTr("Save song") + "    " + backend.keys("Ctrl S"); onTriggered: backend.saveSong() }
                                        MenuItem { text: qsTr("Save as…") + "    " + backend.keys("Ctrl Shift S"); onTriggered: backend.saveSongAs() }
                                        MenuItem { text: qsTr("Open song…") + "    " + backend.keys("Ctrl O"); onTriggered: backend.openSong() }
                                        MenuItem { text: qsTr("Export stems and MIDI…") + "    " + backend.keys("Ctrl E"); onTriggered: win.exportVisible = true }
                                        MenuSeparator {}
                                        MenuItem { text: qsTr("Start over") + "    " + backend.keys("Ctrl N ×2"); onTriggered: backend.startOver() }
                                    }
                                }
                                PanelButton {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    // While it runs, the tutorial's own panel has Leave: the room goes to its text.
                                    visible: !backend.tutorialActive && win.lv >= 2
                                    label: backend.tutorialActive ? qsTr("Leave tutorial") : qsTr("Tutorial: make a track")
                                    key: "Ctrl T"; color: win.green; active: backend.tutorialActive
                                    onClicked: backend.toggleTutorial()
                                }
                                // The loop Space last made on this page, longer or
                                // shorter, until another loop of its part is made:
                                // ↑ ↓ do the same for three seconds after Space (on
                                // Drums, at any time).
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && backend.loopBeats > 0 && !backend.tutorialActive // ↑ ↓ still work; the tutorial needs the room
                                    opacity: backend.loopShorterBeats > 0 ? 1 : 0.35
                                    label: qsTr("Loop shorter")
                                    key: backend.loopShorterBeats > 0 ? win.barsText(backend.loopShorterBeats) : "—"
                                    color: win.pageColor
                                    onClicked: if (backend.loopShorterBeats > 0) backend.resizeLoop(-1)
                                }
                                PanelButton {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                                    visible: win.lv >= 1 && backend.loopBeats > 0 && !backend.tutorialActive // ↑ ↓ still work; the tutorial needs the room
                                    opacity: backend.loopLongerBeats > 0 ? 1 : 0.35
                                    label: qsTr("Loop longer")
                                    key: backend.loopLongerBeats > 0 ? win.barsText(backend.loopLongerBeats) : "—"
                                    color: win.pageColor
                                    onClicked: if (backend.loopLongerBeats > 0) backend.resizeLoop(1)
                                }
                        }
                    }
                }
            }

            // Status in inverse video across the bottom: the pad, then what
            // the showing page says its fingers are doing.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: win.pageColor
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 16
                    width: parent.width - 32
                    elide: Text.ElideRight
                    // Below Studio the NEXT box is the coach: the bar says
                    // only what the page and the studio report.
                    text: win.lv < 2 && (backend.padState === Backend.Idle || backend.padState === Backend.Ready || backend.padState === Backend.Playing)
                          ? (backend.soundIndex >= 0 ? qsTr("Sound: %1").arg(backend.soundNames[backend.soundIndex] || "")
                             : backend.pageStatus !== "" ? backend.pageStatus : " ")
                            + (backend.audioOk || backend.audioError === "" ? "" : "     " + qsTr("No sound: %1").arg(backend.audioError))
                            + (backend.savedNotice !== "" ? "     " + qsTr("Saved %1").arg(backend.savedNotice) : "")
                          : ((backend.padState === Backend.Playing ? qsTr("Playing")
                          : backend.padState === Backend.Starting
                            ? (backend.padNeedsPassword ? qsTr("Waiting for your password") : qsTr("Waiting for the trackpad"))
                          : backend.padState === Backend.Failed ? qsTr("Fault — %1").arg(backend.error)
                          : backend.padState === Backend.Ready
                            ? (backend.padAway ? qsTr("Trackpad given back until tenfold is in front again") : qsTr("Trackpad given back, the sound plays on"))
                          : qsTr("Standby: take the trackpad to play"))
                          + ((backend.pagesSilenced[backend.page] || "") !== ""
                             ? "     " + qsTr("Silent: %1 on the Song page").arg(backend.pagesSilenced[backend.page]) : "")
                          + (backend.pageStatus !== "" ? "     " + backend.pageStatus : "")
                          + (backend.audioOk || backend.audioError === "" ? "" : "     " + qsTr("No sound: %1").arg(backend.audioError))
                          + (backend.savedNotice !== "" ? "     " + qsTr("Saved %1").arg(backend.savedNotice) : ""))
                    color: "#0a0806"
                    font.family: win.mono
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.letterSpacing: 1
                    font.capitalization: Font.AllUppercase
                }
            }
        }

        // A notice from a page or a key, big for a moment over the pad (not
        // the buttons beside it), each letter edged thickly in black so it
        // reads over whatever the pad is lighting up. It wraps to a readable width, up to three
        // lines, its size chosen by how much it says. The pad sits inside
        // layouts, so its place is mapped here; naming its size and the
        // window's makes the binding follow them.
        Item {
            id: noticeBox
            readonly property point padAt: (surface.x, surface.y, surface.width, surface.height, win.width, win.height,
                                            surface.mapToItem(parent, 0, 0))
            readonly property int chars: backend.notice.length
            readonly property int px: Math.round(Math.min(surface.height / 9, chars <= 28 ? 46 : chars <= 56 ? 38 : 30))
            // About 26 letters a line at most: easy to read at a glance.
            width: Math.min(surface.width - 100, px * 0.62 * 26)
            height: noticeLabel.contentHeight
            x: padAt.x + (surface.width - width) / 2
            y: padAt.y + (surface.height - height) / 2
            opacity: win.pinNotice ? 1 : 0
            visible: opacity > 0
            // A thick black edge round every letter (the words drawn in
            // black a few pixels out all round, under them), so they read
            // over whatever the pad is lighting up, with the pad still seen.
            Repeater {
                model: 16
                delegate: Text {
                    required property int index
                    readonly property real angle: index * Math.PI / 8
                    readonly property real reach: Math.max(3, noticeBox.px / 9)
                    x: noticeLabel.x + Math.cos(angle) * reach
                    y: noticeLabel.y + Math.sin(angle) * reach
                    width: noticeLabel.width
                    horizontalAlignment: noticeLabel.horizontalAlignment
                    wrapMode: noticeLabel.wrapMode
                    maximumLineCount: noticeLabel.maximumLineCount
                    elide: noticeLabel.elide
                    lineHeight: noticeLabel.lineHeight
                    textFormat: Text.StyledText
                    text: backend.rich(backend.notice, "#000000", noticeBox.px)
                    font: noticeLabel.font
                    color: "#000000"
                }
            }
            Text {
                id: noticeLabel
                width: noticeBox.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                lineHeight: 1.05
                textFormat: Text.StyledText
                text: backend.rich(backend.notice, win.pageColor, noticeBox.px)
                font.family: win.wide
                font.pixelSize: noticeBox.px
                font.weight: Font.Black
                font.letterSpacing: 1
                font.capitalization: Font.AllUppercase
                color: win.pageColor
                style: Text.Outline
                styleColor: "#000000"
            }
            Connections {
                target: backend
                function onNoticeChanged() { noticeFade.restart() }
            }
            SequentialAnimation {
                id: noticeFade
                NumberAnimation { target: noticeBox; property: "opacity"; to: 1; duration: 60 }
                // Longer to read when it says more.
                PauseAnimation { duration: Math.min(4000, 1300 + noticeBox.chars * 25) }
                NumberAnimation { target: noticeBox; property: "opacity"; to: 0; duration: 500 }
            }
        }

        // The sound browser, over the pad and its band.
        SoundBrowser {
            readonly property point padAt: (surface.x, surface.y, surface.width, surface.height, win.width, win.height,
                                            surface.mapToItem(parent, 0, 0))
            x: padAt.x + 22
            y: padAt.y + 2
            width: surface.width - 44
            height: surface.height - 6
            z: 40
            visible: win.soundsVisible && backend.soundNames.length > 0
            tint: win.pageColor
            pageName: backend.pageNames[backend.page]
            onClosed: win.soundsVisible = false
        }
        // A page without sounds closes the browser; a SOUND or "+N MORE"
        // chip under the pad opens it.
        Connections {
            target: backend
            function onPageChanged() { if (backend.soundNames.length === 0) win.soundsVisible = false }
            function onSoundsAsked() { if (backend.soundNames.length > 0) win.soundsVisible = true }
        }

        HelpPage {
            anchors.fill: parent
            visible: win.helpVisible
            title: win.pageKey(backend.page) + " " + backend.pageNames[backend.page]
            tint: win.pageColor
            pageHelp: backend.pageHelp
        }
    }

    // The first start: a style to begin from.
    Welcome {
        anchors.fill: parent
        visible: backend.welcome
        onTutorial: backend.toggleTutorial()
        onVisibleChanged: if (!visible) screen.forceActiveFocus()
    }

    // Export, over everything but the glass's scanlines.
    ExportDialog {
        id: exportDialog
        anchors.fill: parent
        visible: win.exportVisible
        onClosed: win.exportVisible = false
    }

    // Scanlines over the glass.
    Canvas {
        anchors.fill: parent
        opacity: 0.22
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "#000000"
            for (let y = 0; y < height; y += 3)
                ctx.fillRect(0, y, width, 1)
        }
    }

    // The visualizer's own window: Return on the Visual page, or its button.
    Loader {
        id: visualWindow
        active: false
        sourceComponent: VisualWindow {
            visible: true
            onClosing: visualWindow.active = false
            onBack: win.back()
        }
    }
    // The picture's resolution follows the frame time while it shows here.
    onFrameSwapped: if (visual.showing && !visualWindow.active) visual.frameDrawn()
    Connections {
        target: visual
        function onWindowAsked() { visualWindow.active = !visualWindow.active }
        function onFullScreenAsked() {
            visualWindow.active = true
            visualWindow.item.toggleFullScreen(win.screen)
        }
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Stop recording and quit?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Qt.quit()
        Label { text: qsTr("The recording so far is saved.") }
    }
}
