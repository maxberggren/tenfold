#include "songfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "session.h"
#include "tuning.h"

namespace tf {

namespace {

// A song file bigger than this is not one tenfold wrote: every clip of
// every part full is a few megabytes, and the Vocal page's audio about a
// megabyte for every four seconds of loops.
constexpr qint64 kMaxFileBytes = 512ll * 1024 * 1024;

// The order keys are written in, so a file reads top down: what it is, the
// song's settings, then the parts, and in each the small things before the
// long lists. Keys not named here follow, alphabetically.
const char *const kKeyOrder[] = {
    "tenfold", "name", "tempo", "key", "metronome", "page", "master", "slot", "beats", "anchor",
    "at", "type", "flags", "layer", "id", "i", "f", "root", "family", "brightness",
    "mix", "settings", "playing", "clips", "audio", "events", "parts", "song",
};

int keyRank(const QString &key) {
    for (size_t i = 0; i < std::size(kKeyOrder); ++i)
        if (key == QLatin1String(kKeyOrder[i]))
            return int(i);
    return int(std::size(kKeyOrder));
}

QStringList orderedKeys(const QJsonObject &o) {
    QStringList keys = o.keys();
    std::stable_sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        const int ra = keyRank(a), rb = keyRank(b);
        return ra != rb ? ra < rb : a < b;
    });
    return keys;
}

QByteArray scalar(const QJsonValue &v) {
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (!std::isfinite(d))
            return "null";
        if (d == std::floor(d) && std::fabs(d) < 1e15)
            return QByteArray::number(qint64(d));
        return QByteArray::number(d, 'g', QLocale::FloatingPointShortest);
    }
    // Strings, booleans and null as Qt writes them (escapes included).
    const QByteArray s = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return s.mid(1, s.size() - 2);
}

QByteArray oneLine(const QJsonValue &v) {
    if (v.isArray()) {
        QByteArray out = "[";
        const QJsonArray a = v.toArray();
        for (qsizetype i = 0; i < a.size(); ++i)
            out += (i ? ", " : "") + oneLine(a[i]);
        return out + "]";
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        QByteArray out = "{";
        bool first = true;
        for (const QString &k : orderedKeys(o)) {
            out += (first ? "" : ", ") + scalar(k) + ": " + oneLine(o[k]);
            first = false;
        }
        return out + "}";
    }
    return scalar(v);
}

void write(const QJsonValue &v, int indent, QByteArray &out) {
    const QByteArray line = oneLine(v);
    if ((!v.isArray() && !v.isObject()) || indent + line.size() <= 120) {
        out += line;
        return;
    }
    const QByteArray pad(indent + 2, ' ');
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        out += "[\n";
        for (qsizetype i = 0; i < a.size(); ++i) {
            out += pad;
            write(a[i], indent + 2, out);
            out += i + 1 < a.size() ? ",\n" : "\n";
        }
        out += QByteArray(indent, ' ') + "]";
        return;
    }
    const QJsonObject o = v.toObject();
    const QStringList keys = orderedKeys(o);
    out += "{\n";
    for (qsizetype i = 0; i < keys.size(); ++i) {
        out += pad + scalar(keys[i]) + ": ";
        write(o[keys[i]], indent + 2, out);
        out += i + 1 < keys.size() ? ",\n" : "\n";
    }
    out += QByteArray(indent, ' ') + "}";
}


const char *typeName(uint16_t type) {
    switch (type) {
    case Command::On: return "on";
    case Command::Off: return "off";
    case Command::Move: return "move";
    case Command::Param: return "param";
    default: return nullptr;
    }
}

bool number(const QJsonValue &v, double *out) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        return false;
    *out = v.toDouble();
    return true;
}

bool integer(const QJsonValue &v, double lo, double hi, int64_t *out) {
    double d = 0;
    if (!number(v, &d) || d != std::floor(d) || d < lo || d > hi)
        return false;
    *out = int64_t(d);
    return true;
}

}

// Through decimal text rather than arithmetic: the build's fast maths turns
// a division by a million into a multiplication that misses by an ulp. The
// shortest decimal that reads back as the same float, found by printf
// (std::to_chars for floats needs macOS 13.3).
double tidyFloat(float f) {
    if (!std::isfinite(f))
        return f;
    char buf[64];
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, double(f));
        if (std::strtof(buf, nullptr) == f)
            break;
    }
    return std::strtod(buf, nullptr);
}

double tidyNumber(double v, int decimals) {
    if (!std::isfinite(v))
        return v;
    char buf[400];
    if (std::snprintf(buf, sizeof buf, "%.*f", decimals, v) >= int(sizeof buf))
        return v;
    return std::strtod(buf, nullptr);
}

QByteArray songJson(const QJsonObject &doc) {
    QByteArray out;
    write(doc, 0, out);
    return out + "\n";
}

bool checkSongFormat(const QJsonObject &doc, QString *error) {
    const QJsonValue v = doc.value(QStringLiteral("tenfold"));
    int64_t format = 0;
    if (!integer(v, 1, 1e9, &format)) {
        if (error)
            *error = QStringLiteral("This isn't a tenfold song");
        return false;
    }
    if (format > kSongFormat) {
        if (error)
            *error = QStringLiteral("This song was saved by a newer tenfold (format %1, this one reads %2): update tenfold to open it")
                         .arg(format)
                         .arg(kSongFormat);
        return false;
    }
    return true;
}

bool readSongFile(const QString &path, QJsonObject *doc, QString *error) {
    const QString name = QFileInfo(path).fileName();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Can't open %1: %2").arg(name, f.errorString());
        return false;
    }
    if (f.size() > kMaxFileBytes) {
        if (error)
            *error = QStringLiteral("%1 is too big to be a tenfold song").arg(name);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        if (error)
            *error = QStringLiteral("%1 isn't a tenfold song (%2)")
                         .arg(name, pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not an object"));
        return false;
    }
    if (!checkSongFormat(d.object(), error))
        return false;
    *doc = d.object();
    return true;
}

bool writeSongFile(const QString &path, const QJsonObject &doc, QString *error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(songJson(doc)) < 0 || !f.commit()) {
        if (error)
            *error = QStringLiteral("Can't save %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    return true;
}

QJsonObject clipToJson(int slot, const ClipSet::Clip &clip) {
    QJsonObject o;
    o[QStringLiteral("slot")] = slot;
    o[QStringLiteral("beats")] = tidyNumber(clip.length, 6);
    double anchor = std::fmod(clip.anchor, clip.length);
    if (anchor < 0)
        anchor += clip.length;
    if (anchor >= clip.length - 1e-9)
        anchor = 0;
    o[QStringLiteral("anchor")] = std::min(tidyNumber(anchor, 6), clip.length);
    QJsonArray events;
    for (const ClipEvent &e : clip.events) {
        QJsonObject j;
        // To a millionth of a beat (half a microsecond at 120 BPM), which
        // reads 0.9988 instead of 0.9988000001906876; never rounded up onto
        // the clip's end, where it would no longer be inside it.
        double at = tidyNumber(e.pos, 6);
        if (at >= clip.length || at < 0)
            at = e.pos;
        j[QStringLiteral("at")] = at;
        if (const char *name = typeName(e.c.type))
            j[QStringLiteral("type")] = QLatin1String(name);
        else
            j[QStringLiteral("type")] = int(e.c.type);
        if (e.c.flags)
            j[QStringLiteral("flags")] = int(e.c.flags);
        if (e.layer)
            j[QStringLiteral("layer")] = e.layer;
        j[QStringLiteral("id")] = e.c.id;
        int ni = 4, nf = 8;
        while (ni > 0 && e.c.i[ni - 1] == 0)
            --ni;
        while (nf > 0 && e.c.f[nf - 1] == 0.0f)
            --nf;
        if (ni) {
            QJsonArray a;
            for (int k = 0; k < ni; ++k)
                a.append(e.c.i[k]);
            j[QStringLiteral("i")] = a;
        }
        if (nf) {
            QJsonArray a;
            for (int k = 0; k < nf; ++k)
                a.append(tidyFloat(e.c.f[k]));
            j[QStringLiteral("f")] = a;
        }
        events.append(j);
    }
    o[QStringLiteral("events")] = events;
    return o;
}

bool clipFromJson(const QJsonValue &v, int *slot, ClipSet::Clip *clip, QString *error) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!v.isObject())
        return fail(QStringLiteral("a clip isn't an object"));
    const QJsonObject o = v.toObject();
    int64_t s = 0;
    if (!integer(o[QStringLiteral("slot")], 0, kClipSlots - 1, &s))
        return fail(QStringLiteral("a clip has no slot from 0 to 7"));
    double length = 0, anchor = 0;
    if (!number(o[QStringLiteral("beats")], &length) || length < 0.25 || length > 256)
        return fail(QStringLiteral("clip %1 has no length in beats").arg(s + 1));
    if (o.contains(QStringLiteral("anchor")) && !number(o[QStringLiteral("anchor")], &anchor))
        return fail(QStringLiteral("clip %1's anchor isn't a number").arg(s + 1));
    const QJsonArray events = o[QStringLiteral("events")].toArray();
    if (events.size() > kClipEvents)
        return fail(QStringLiteral("clip %1 has more than %2 events").arg(s + 1).arg(kClipEvents));
    ClipSet::Clip c;
    c.length = length;
    c.anchor = std::clamp(anchor, 0.0, length);
    c.events.reserve(size_t(kClipEvents));
    for (qsizetype n = 0; n < events.size(); ++n) {
        const QString where = QStringLiteral("clip %1, event %2").arg(s + 1).arg(n + 1);
        if (!events[n].isObject())
            return fail(where + QStringLiteral(" isn't an object"));
        const QJsonObject j = events[n].toObject();
        ClipEvent e;
        if (!number(j[QStringLiteral("at")], &e.pos) || e.pos < 0 || e.pos >= length)
            return fail(where + QStringLiteral(" isn't inside the clip"));
        const QJsonValue type = j[QStringLiteral("type")];
        int64_t t = 0;
        if (type.isString()) {
            const QString name = type.toString();
            t = name == QLatin1String("on")      ? Command::On
                : name == QLatin1String("off")   ? Command::Off
                : name == QLatin1String("move")  ? Command::Move
                : name == QLatin1String("param") ? Command::Param
                                                 : 0;
        } else if (!integer(type, Command::User, 65535, &t)) {
            t = 0;
        }
        if (t == 0)
            return fail(where + QStringLiteral(" has no type tenfold knows"));
        e.c.type = uint16_t(t);
        int64_t x = 0;
        if (j.contains(QStringLiteral("flags"))) {
            if (!integer(j[QStringLiteral("flags")], 0, 65535, &x))
                return fail(where + QStringLiteral("'s flags aren't a number"));
            e.c.flags = uint16_t(x);
        }
        if (j.contains(QStringLiteral("layer"))) {
            if (!integer(j[QStringLiteral("layer")], 0, kClipLayers - 1, &x))
                return fail(where + QStringLiteral("'s layer isn't from 0 to %1").arg(kClipLayers - 1));
            e.layer = int(x);
        }
        // A sound's id stays below 1 << 24: replayed clips set the bits above.
        if (!integer(j[QStringLiteral("id")], -(1 << 24), (1 << 24) - 1, &x))
            return fail(where + QStringLiteral(" has no id"));
        e.c.id = int32_t(x);
        const QJsonValue iv = j[QStringLiteral("i")], fv = j[QStringLiteral("f")];
        if ((!iv.isUndefined() && !iv.isArray()) || iv.toArray().size() > 4)
            return fail(where + QStringLiteral(" has more than four whole numbers"));
        if ((!fv.isUndefined() && !fv.isArray()) || fv.toArray().size() > 8)
            return fail(where + QStringLiteral(" has more than eight values"));
        const QJsonArray ia = iv.toArray(), fa = fv.toArray();
        for (qsizetype k = 0; k < ia.size(); ++k) {
            if (!integer(ia[k], INT32_MIN, INT32_MAX, &x))
                return fail(where + QStringLiteral(" has a whole number that isn't one"));
            e.c.i[k] = int32_t(x);
        }
        for (qsizetype k = 0; k < fa.size(); ++k) {
            double d = 0;
            if (!number(fa[k], &d) || std::fabs(d) > 1e6)
                return fail(where + QStringLiteral(" has a value that isn't a number"));
            e.c.f[k] = float(d);
        }
        c.events.push_back(e);
    }
    // The studio plays a clip's events in order.
    std::stable_sort(c.events.begin(), c.events.end(), [](const ClipEvent &a, const ClipEvent &b) { return a.pos < b.pos; });
    *slot = int(s);
    *clip = std::move(c);
    return true;
}

QString songSummary(const QJsonObject &doc) {
    int clips = 0, events = 0;
    QStringList parts;
    for (const QJsonValue &p : doc[QStringLiteral("parts")].toArray()) {
        const QJsonArray c = p[QStringLiteral("clips")].toArray();
        if (!c.isEmpty())
            parts << p[QStringLiteral("name")].toString();
        clips += int(c.size());
        for (const QJsonValue &clip : c)
            events += int(clip[QStringLiteral("events")].toArray().size());
    }
    const QString key = doc[QStringLiteral("key")][QStringLiteral("name")].toString();
    return QStringLiteral("format %1, %2 BPM%3, %4 clips (%5 events)%6")
        .arg(doc[QStringLiteral("tenfold")].toInt())
        .arg(doc[QStringLiteral("tempo")].toDouble())
        .arg(key.isEmpty() ? QString() : QStringLiteral(", ") + key)
        .arg(clips)
        .arg(events)
        .arg(parts.isEmpty() ? QString() : QStringLiteral(": ") + parts.join(QStringLiteral(", ")));
}

// --- the Ctrl+S / Ctrl+O flow --------------------------------------------------

SongFiles::SongFiles(Session *session, FilePicker *picker) : m_session(session), m_picker(picker) {
    const QString state = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    if (!state.isEmpty())
        m_autosave = state + QStringLiteral("/tenfold/last.tenfold");
}

void SongFiles::save() {
    if (m_path.isEmpty())
        saveAs();
    else
        saveTo(m_path);
}

QString songFileName(double bpm, int root, const QString &mode, const QDateTime &when) {
    QString key = QString::fromUtf8(Tuning::noteName(root));
    key.replace(QStringLiteral("♯"), QStringLiteral("#")).replace(QStringLiteral("♭"), QStringLiteral("b"));
    return QStringLiteral("%1 · %2 bpm · %3 %4.tenfold")
        .arg(when.toString(QStringLiteral("yyyy-MM-dd HH.mm")))
        .arg(int(std::lround(bpm)))
        .arg(key, mode.toLower());
}

void SongFiles::saveAs() {
    if (!m_picker)
        return;
    const QFileInfo was(m_path);
    const QString folder = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                                            : was.absolutePath();
    const StudioView &v = m_session->view();
    const ScaleFamily &f = family(v.family);
    const QString mode = QString::fromUtf8(f.modeNames[std::clamp(v.brightness, 0, f.modeCount - 1)]);
    const QString name = m_path.isEmpty() ? songFileName(v.bpm, v.root, mode, QDateTime::currentDateTime())
                                          : was.fileName();
    m_picker->saveFile(QStringLiteral("Save song"), name, folder, [this](const QString &path) {
        if (path.isEmpty())
            return;
        // The dialog may hand back a name typed without the extension.
        saveTo(path.endsWith(QStringLiteral(".tenfold")) ? path : path + QStringLiteral(".tenfold"));
    });
}

void SongFiles::open() {
    if (!m_picker)
        return;
    m_picker->openFile(QStringLiteral("Open song"), [this](const QString &path) {
        if (!path.isEmpty())
            openFrom(path);
    });
}

bool SongFiles::saveTo(const QString &path) {
    QString error;
    const QJsonObject doc = m_session->saveSong(&error);
    if (doc.isEmpty() || !writeSongFile(path, doc, &error)) {
        m_session->say(error);
        return false;
    }
    m_path = path;
    m_session->say(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool SongFiles::openFrom(const QString &path) {
    QJsonObject doc;
    QString error;
    if (!readSongFile(path, &doc, &error) || !m_session->openSong(doc, &error)) {
        m_session->say(error);
        return false;
    }
    // The song kept on quit is opened as a copy: Ctrl+S asks where to put it.
    const bool kept = path == m_autosave;
    m_path = kept ? QString() : path;
    // A song that opened with something left out says so after its name.
    m_session->say((kept ? QStringLiteral("Picked up where you left off · Ctrl+S saves it")
                         : QStringLiteral("Opened %1").arg(QFileInfo(path).fileName())) +
                   (error.isEmpty() ? QString() : QStringLiteral(" · ") + error));
    return true;
}

void SongFiles::reopenLast() {
    if (!hasAutosave()) {
        m_session->say(QStringLiteral("No song kept from last time"));
        return;
    }
    openFrom(m_autosave);
}

bool SongFiles::autosave() {
    if (m_autosave.isEmpty() || !m_session->anythingMade())
        return false;
    QDir().mkpath(QFileInfo(m_autosave).absolutePath());
    QString error;
    const QJsonObject doc = m_session->saveSong(&error);
    return !doc.isEmpty() && writeSongFile(m_autosave, doc, &error);
}

bool SongFiles::hasAutosave() const { return !m_autosave.isEmpty() && QFileInfo::exists(m_autosave); }

}
