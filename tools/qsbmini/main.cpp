// qsbmini: bakes one Vulkan-style GLSL 440 fragment shader into a .qsb that
// Qt Quick's ShaderEffect loads, without Qt's qsb (qt6-shadertools).
//
//   qsbmini in.frag out.qsb [--glslc /usr/bin/glslc] [--dump DIR]
//
// --dump writes each GLSL variant beside the .qsb's name in DIR, for
// glslangValidator to check (bin/shaders does).
//
// What qsb does with SPIRV-Cross, done here by rewriting the source, which
// works because the visualizer's shaders keep to a plain subset: one
// std140 uniform block named "buf" with instance "ubuf", combined 2D
// samplers, one vec2 input and one vec4 output. It writes
//
//   GLSL 120, 150, 330 and 300 es   OpenGL (Linux, the default there) and GLES
//   SPIR-V 1.0                      Vulkan, when glslc is found
//
// and the reflection Qt needs to bind uniforms by name. No Metal: the macOS
// bundle is baked with the real qsb (bin/bundle-macos runs bin/shaders,
// which prefers it), since SPIRV-Cross's MSL is the one Qt is tested with.
//
// Uses QShaderDescription's private half to fill in the reflection: this is
// a developer's tool run on one machine, not something that ships.

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cstdio>
#include <private/qshaderdescription_p.h>
#include <rhi/qshader.h>

namespace {

struct TypeInfo {
    QShaderDescription::VariableType type;
    int align, size, matrixStride;
};

bool typeInfo(const QString &t, TypeInfo *out) {
    using V = QShaderDescription;
    static const struct {
        const char *name;
        TypeInfo info;
    } table[] = {
        {"float", {V::Float, 4, 4, 0}},   {"int", {V::Int, 4, 4, 0}},       {"vec2", {V::Vec2, 8, 8, 0}},
        {"vec3", {V::Vec3, 16, 12, 0}},   {"vec4", {V::Vec4, 16, 16, 0}},   {"mat2", {V::Mat2, 16, 32, 16}},
        {"mat3", {V::Mat3, 16, 48, 16}},  {"mat4", {V::Mat4, 16, 64, 16}},
    };
    for (const auto &e : table)
        if (t == QLatin1String(e.name)) {
            *out = e.info;
            return true;
        }
    return false;
}

QShaderDescription::VariableType ioType(const QString &t) {
    TypeInfo i{};
    return typeInfo(t, &i) ? i.type : QShaderDescription::Unknown;
}

int fail(const QString &why) {
    std::fprintf(stderr, "qsbmini: %s\n", qPrintable(why));
    return 1;
}

// The source for one GLSL version: the 440 interface rewritten the way
// SPIRV-Cross writes it for OpenGL (the uniform block as a struct of plain
// uniforms, which is how Qt's OpenGL backend sets them: "ubuf.member").
QByteArray glslFor(const QString &src, int version, bool es) {
    QString s = src;
    const bool modern = es || version >= 130;
    s.replace(QRegularExpression(QStringLiteral("^#version\\s+\\d+.*$"), QRegularExpression::MultilineOption),
              es ? QStringLiteral("#version %1 es\nprecision highp float;\nprecision highp int;").arg(version)
                 : QStringLiteral("#version %1").arg(version));
    // layout(std140, binding = 0) uniform buf { ... } ubuf;  →  struct buf { ... }; uniform buf ubuf;
    s.replace(QRegularExpression(QStringLiteral("layout\\s*\\([^)]*\\)\\s*uniform\\s+(\\w+)\\s*\\{([^}]*)\\}\\s*(\\w+)\\s*;")),
              QStringLiteral("struct \\1 {\\2};\nuniform \\1 \\3;"));
    s.replace(QRegularExpression(QStringLiteral("layout\\s*\\([^)]*\\)\\s*uniform\\s+sampler2D")),
              QStringLiteral("uniform sampler2D"));
    s.replace(QRegularExpression(QStringLiteral("layout\\s*\\([^)]*\\)\\s*in\\s+")),
              modern ? QStringLiteral("in ") : QStringLiteral("varying "));
    const QRegularExpression out(QStringLiteral("layout\\s*\\([^)]*\\)\\s*out\\s+vec4\\s+(\\w+)\\s*;"));
    if (modern)
        s.replace(out, version >= 330 || es ? QStringLiteral("layout(location = 0) out vec4 \\1;")
                                             : QStringLiteral("out vec4 \\1;"));
    else
        s.replace(out, QStringLiteral("#define \\1 gl_FragColor"));
    if (!modern)
        s.replace(QRegularExpression(QStringLiteral("\\btexture\\s*\\(")), QStringLiteral("texture2D("));
    return s.toUtf8();
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3)
        return fail(QStringLiteral("usage: qsbmini in.frag out.qsb [--glslc PATH]"));
    QString glslc = QStringLiteral("glslc");
    if (const int i = args.indexOf(QStringLiteral("--glslc")); i > 0 && i + 1 < args.size())
        glslc = args[i + 1];

    QFile in(args[1]);
    if (!in.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot read %1").arg(args[1]));
    const QString src = QString::fromUtf8(in.readAll());

    QShaderDescription desc;
    QShaderDescriptionPrivate *d = QShaderDescriptionPrivate::get(&desc);

    // Inputs and the output.
    const QRegularExpression io(QStringLiteral("layout\\s*\\(\\s*location\\s*=\\s*(\\d+)\\s*\\)\\s*(in|out)\\s+(\\w+)\\s+(\\w+)\\s*;"));
    for (auto it = io.globalMatch(src); it.hasNext();) {
        const auto m = it.next();
        QShaderDescription::InOutVariable v;
        v.name = m.captured(4).toUtf8();
        v.type = ioType(m.captured(3));
        v.location = m.captured(1).toInt();
        (m.captured(2) == QLatin1String("in") ? d->inVars : d->outVars).append(v);
    }

    // The uniform block, laid out by std140.
    const QRegularExpression block(QStringLiteral("layout\\s*\\(\\s*std140\\s*,\\s*binding\\s*=\\s*(\\d+)\\s*\\)\\s*uniform\\s+(\\w+)\\s*\\{([^}]*)\\}\\s*(\\w+)\\s*;"));
    const auto bm = block.match(src);
    if (bm.hasMatch()) {
        QShaderDescription::UniformBlock ub;
        ub.binding = bm.captured(1).toInt();
        ub.descriptorSet = 0;
        ub.blockName = bm.captured(2).toUtf8();
        ub.structName = bm.captured(4).toUtf8();
        int offset = 0;
        const QRegularExpression member(QStringLiteral("(\\w+)\\s+(\\w+)\\s*;"));
        // Comments inside the block are dropped before reading its members.
        QString body = bm.captured(3);
        body.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
        for (auto it = member.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            TypeInfo t{};
            if (!typeInfo(m.captured(1), &t))
                return fail(QStringLiteral("%1: a uniform of a type qsbmini doesn't lay out: %2").arg(args[1], m.captured(1)));
            offset = (offset + t.align - 1) / t.align * t.align;
            QShaderDescription::BlockVariable v;
            v.name = m.captured(2).toUtf8();
            v.type = t.type;
            v.offset = offset;
            v.size = t.size;
            v.matrixStride = t.matrixStride;
            ub.members.append(v);
            offset += t.size;
        }
        ub.size = (offset + 15) / 16 * 16;
        d->uniformBlocks.append(ub);
    }

    const QRegularExpression sampler(QStringLiteral("layout\\s*\\(\\s*binding\\s*=\\s*(\\d+)\\s*\\)\\s*uniform\\s+sampler2D\\s+(\\w+)\\s*;"));
    for (auto it = sampler.globalMatch(src); it.hasNext();) {
        const auto m = it.next();
        QShaderDescription::InOutVariable v;
        v.name = m.captured(2).toUtf8();
        v.type = QShaderDescription::Sampler2D;
        v.binding = m.captured(1).toInt();
        v.descriptorSet = 0;
        d->combinedImageSamplers.append(v);
    }

    QShader shader;
    shader.setStage(QShader::FragmentStage);
    shader.setDescription(desc);
    const int dumpAt = args.indexOf(QStringLiteral("--dump"));
    const QString dump = dumpAt > 0 && dumpAt + 1 < args.size() ? args[dumpAt + 1] : QString();
    const struct {
        int version;
        bool es;
    } variants[] = {{120, false}, {150, false}, {330, false}, {300, true}};
    for (const auto &v : variants) {
        const QByteArray code = glslFor(src, v.version, v.es);
        shader.setShader(QShaderKey(QShader::GlslShader,
                                    QShaderVersion(v.version, v.es ? QShaderVersion::GlslEs : QShaderVersion::Flags())),
                         QShaderCode(code));
        if (!dump.isEmpty()) {
            QFile f(QStringLiteral("%1/%2.%3%4.frag")
                        .arg(dump, QFileInfo(args[1]).completeBaseName())
                        .arg(v.version)
                        .arg(v.es ? QStringLiteral("es") : QString()));
            if (f.open(QIODevice::WriteOnly))
                f.write(code);
        }
    }

    // SPIR-V for Vulkan, straight from the source, when glslc is here.
    QTemporaryDir tmp;
    QProcess p;
    const QString spv = tmp.path() + QStringLiteral("/out.spv");
    p.start(glslc, {QStringLiteral("-fshader-stage=frag"), QStringLiteral("--target-env=vulkan1.0"), QStringLiteral("-O"),
                    args[1], QStringLiteral("-o"), spv});
    if (p.waitForFinished(60000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        QFile f(spv);
        if (f.open(QIODevice::ReadOnly))
            shader.setShader(QShaderKey(QShader::SpirvShader, QShaderVersion(100)), QShaderCode(f.readAll(), "main"));
    } else if (p.error() != QProcess::FailedToStart) {
        // glslc is here and said no: the source itself is wrong.
        return fail(QStringLiteral("%1 does not compile:\n%2").arg(args[1], QString::fromUtf8(p.readAllStandardError())));
    }

    // The 6.5 format, which every Qt from 6.5 on reads (the CI's 6.8 too).
    QFile out(args[2]);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1").arg(args[2]));
    out.write(shader.serialized(QShader::SerializedFormatVersion::Qt_6_5));
    return 0;
}
