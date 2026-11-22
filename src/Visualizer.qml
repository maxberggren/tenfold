import QtQuick
import Tenfold

// The picture, as four passes of shaders (src/visual/*.frag):
//
//   scene     the raymarched world, at a fraction of the size (the page's
//             quality), since it is the costly one
//   feedback  last frame fed back into this one, zoomed, turned, swirled
//             by the fingers, with the 2D marks of the parts drawn in;
//             a 16-bit float texture so the trails fade smoothly
//   blur ×2   a quarter-size bloom of what is bright
//   post      the glass: bloom, fringes, curve, scanlines, grain, the
//             drop's flash
//
// Every number comes from `visual.u`, which the app steps once a frame
// (visualizer.h): one instance or three (the main window, the second
// window, the video) all show the same music the same way.
Item {
    id: root
    // How much CRT the glass shows: the main window has scanlines of its own.
    property real crt: 1.0
    // For looking at one pass alone (TENFOLD_VISUAL_STAGE: 1 the world,
    // 2 the feedback without bloom); 0 the whole picture.
    property int stage: 0
    readonly property var u: visual.u
    readonly property vector4d res: Qt.vector4d(width, height, u.res.z, u.res.w)
    readonly property vector4d roll: Qt.vector4d(u.roll.x, u.roll.y, u.roll.z, crt)
    clip: true

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    VisualData {
        id: waveform
        source: visual
        width: 1
        height: 1
        opacity: 0
    }

    ShaderEffect {
        id: scene
        width: root.width
        height: root.height
        readonly property vector4d res: root.res
        readonly property vector4d drums: root.u.drums
        readonly property vector4d drums2: root.u.drums2
        readonly property vector4d bass: root.u.bass
        readonly property vector4d chord: root.u.chord
        readonly property vector4d lead: root.u.lead
        readonly property vector4d arp: root.u.arp
        readonly property vector4d atmos: root.u.atmos
        readonly property vector4d fx: root.u.fx
        readonly property vector4d vocal: root.u.vocal
        readonly property vector4d levels0: root.u.levels0
        readonly property vector4d levels1: root.u.levels1
        readonly property vector4d morph0: root.u.morph0
        readonly property vector4d morph1: root.u.morph1
        readonly property vector4d morph2: root.u.morph2
        readonly property vector4d morph3: root.u.morph3
        fragmentShader: "qrc:/visual/scene.frag.qsb"
    }
    ShaderEffectSource {
        id: sceneTexture
        sourceItem: scene
        hideSource: true
        visible: false
        smooth: true
        format: ShaderEffectSource.RGBA16F
        textureSize: Qt.size(Math.max(16, Math.round(root.width * visual.sceneScale)),
                             Math.max(16, Math.round(root.height * visual.sceneScale)))
    }

    ShaderEffect {
        id: feedback
        width: root.width
        height: root.height
        readonly property var scene: sceneTexture
        readonly property var prev: feedbackTexture
        readonly property var wave: waveform
        readonly property vector4d res: root.res
        readonly property vector4d drums: root.u.drums
        readonly property vector4d drums2: root.u.drums2
        readonly property vector4d bass: root.u.bass
        readonly property vector4d chord: root.u.chord
        readonly property vector4d lead: root.u.lead
        readonly property vector4d arp: root.u.arp
        readonly property vector4d atmos: root.u.atmos
        readonly property vector4d fx: root.u.fx
        readonly property vector4d vocal: root.u.vocal
        readonly property vector4d morph0: root.u.morph0
        readonly property vector4d morph1: root.u.morph1
        readonly property vector4d morph2: root.u.morph2
        readonly property vector4d morph3: root.u.morph3
        readonly property vector4d roll: root.roll
        readonly property vector4d f0: root.u.f0
        readonly property vector4d f1: root.u.f1
        readonly property vector4d f2: root.u.f2
        readonly property vector4d f3: root.u.f3
        readonly property vector4d f4: root.u.f4
        readonly property vector4d f5: root.u.f5
        readonly property vector4d f6: root.u.f6
        readonly property vector4d f7: root.u.f7
        readonly property vector4d f8: root.u.f8
        readonly property vector4d f9: root.u.f9
        readonly property vector4d n0: root.u.n0
        readonly property vector4d n1: root.u.n1
        readonly property vector4d n2: root.u.n2
        readonly property vector4d n3: root.u.n3
        readonly property vector4d debug: Qt.vector4d(root.stage, 0, 0, 0)
        fragmentShader: "qrc:/visual/feedback.frag.qsb"
    }
    ShaderEffectSource {
        id: feedbackTexture
        sourceItem: feedback
        hideSource: true
        visible: false
        recursive: true
        live: true
        smooth: true
        format: ShaderEffectSource.RGBA16F
    }

    // The bloom: across, then down, at a quarter of the size.
    ShaderEffect {
        id: blurAcross
        width: Math.max(8, Math.round(root.width / 4))
        height: Math.max(8, Math.round(root.height / 4))
        readonly property var src: feedbackTexture
        readonly property vector4d dir: Qt.vector4d(1 / width, 0, 0.55, 1)
        fragmentShader: "qrc:/visual/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: blurAcrossTexture
        sourceItem: blurAcross
        hideSource: true
        visible: false
        smooth: true
        format: ShaderEffectSource.RGBA16F
    }
    ShaderEffect {
        id: blurDown
        width: blurAcross.width
        height: blurAcross.height
        readonly property var src: blurAcrossTexture
        readonly property vector4d dir: Qt.vector4d(0, 1 / height, -1, 1)
        fragmentShader: "qrc:/visual/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: bloomTexture
        sourceItem: blurDown
        hideSource: true
        visible: false
        smooth: true
        format: ShaderEffectSource.RGBA16F
    }

    Rectangle {
        id: black
        width: 8
        height: 8
        color: "#000000"
    }
    ShaderEffectSource {
        id: blackTexture
        sourceItem: black
        hideSource: true
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent
        readonly property var image: root.stage === 1 ? sceneTexture : feedbackTexture
        readonly property var bloom: root.stage === 0 ? bloomTexture : blackTexture
        readonly property vector4d res: root.res
        readonly property vector4d drums: root.u.drums
        readonly property vector4d chord: root.u.chord
        readonly property vector4d fx: root.u.fx
        readonly property vector4d atmos: root.u.atmos
        readonly property vector4d lead: root.u.lead
        readonly property vector4d drums2: root.u.drums2
        readonly property vector4d morph2: root.u.morph2
        readonly property vector4d roll: root.roll
        fragmentShader: "qrc:/visual/post.frag.qsb"
    }
}
