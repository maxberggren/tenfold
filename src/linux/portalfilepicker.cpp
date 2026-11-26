#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QUrl>

namespace {

// The portal's file filters, a(sa(us)): a name and its patterns, each
// pattern a glob (0) or a MIME type (1).
struct FilterRule {
    uint kind = 0;
    QString pattern;
};
struct Filter {
    QString name;
    QList<FilterRule> rules;
};
using Filters = QList<Filter>;

QDBusArgument &operator<<(QDBusArgument &a, const FilterRule &r) {
    a.beginStructure();
    a << r.kind << r.pattern;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, FilterRule &r) {
    a.beginStructure();
    a >> r.kind >> r.pattern;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const Filter &f) {
    a.beginStructure();
    a << f.name << f.rules;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, Filter &f) {
    a.beginStructure();
    a >> f.name >> f.rules;
    a.endStructure();
    return a;
}

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");

}

Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(Filter)

void PortalRequest::finish(const QString &path) {
    if (m_done) {
        auto done = std::move(m_done);
        m_done = nullptr;
        done(path);
    }
    deleteLater();
}

void PortalRequest::response(uint code, const QVariantMap &results) {
    // 0 is a file picked; 1 the dialog closed; 2 anything else.
    QString path;
    if (code == 0) {
        const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
        if (!uris.isEmpty())
            path = QUrl(uris.first()).toLocalFile();
    }
    finish(path);
}

PortalFilePicker::PortalFilePicker(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<Filters>();
}

void PortalFilePicker::openFile(const QString &title, std::function<void(const QString &)> done) {
    ask(QStringLiteral("OpenFile"), title, {}, std::move(done));
}

void PortalFilePicker::openSound(const QString &title, std::function<void(const QString &)> done) {
    ask(QStringLiteral("OpenFile"), title, {}, std::move(done), true);
}

void PortalFilePicker::saveFile(const QString &title, const QString &name, const QString &folder,
                                std::function<void(const QString &)> done) {
    QVariantMap options;
    options.insert(QStringLiteral("current_name"), name);
    if (!folder.isEmpty()) {
        // A path as bytes, ending in a nul, as the portal wants it.
        QByteArray bytes = QFile::encodeName(folder);
        bytes.append('\0');
        options.insert(QStringLiteral("current_folder"), bytes);
    }
    ask(QStringLiteral("SaveFile"), title, options, std::move(done));
}

void PortalFilePicker::ask(const QString &method, const QString &title, QVariantMap options,
                           std::function<void(const QString &)> done, bool sounds) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (onError)
            onError(QStringLiteral("No file dialog: there is no desktop session bus"));
        done({});
        return;
    }
    const QString token = QStringLiteral("tenfold%1").arg(++m_next);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    auto *request = new PortalRequest(std::move(done), this);
    bus.connect(kService, requestPath, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                request, SLOT(response(uint, QVariantMap)));

    // Songs, or sounds (WAV read as it is, the rest through ffmpeg), and
    // everything else as a second choice.
    QList<FilterRule> soundRules;
    for (const char *ext : {"wav", "mp3", "flac", "ogg", "opus", "m4a", "aac", "aif", "aiff", "mp4", "webm"}) {
        soundRules.append({0, QStringLiteral("*.") + QLatin1String(ext)});
        soundRules.append({0, QStringLiteral("*.") + QString::fromLatin1(ext).toUpper()});
    }
    const Filters filters = sounds ? Filters{{QStringLiteral("Sound files"), soundRules},
                                             {QStringLiteral("All files"), {{0, QStringLiteral("*")}}}}
                                   : Filters{{QStringLiteral("tenfold songs"), {{0, QStringLiteral("*.tenfold")}}}};
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("filters"), QVariant::fromValue(filters));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(filters.first()));

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.portal.FileChooser"), method);
    call << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), request);
    connect(watcher, &QDBusPendingCallWatcher::finished, request, [this, request, requestPath](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        // No portal, or one that refused the call: no dialog will come.
        if (reply.isError()) {
            if (onError)
                onError(QStringLiteral("The file dialog didn't open: %1").arg(reply.error().message()));
            request->finish({});
            return;
        }
        // An older portal may answer on another path than the one asked for.
        const QString path = reply.value().path();
        if (path != requestPath)
            QDBusConnection::sessionBus().connect(kService, path, QStringLiteral("org.freedesktop.portal.Request"),
                                                  QStringLiteral("Response"), request, SLOT(response(uint, QVariantMap)));
    });
}
