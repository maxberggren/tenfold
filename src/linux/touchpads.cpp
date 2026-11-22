#include "touchpads.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>

namespace Device {

QString hyprName(const QString &name) {
    return name.trimmed().toLower().replace(' ', '-');
}

static QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QList<Touchpad> findTouchpads(const QString &sysRoot, const QString &udevRoot) {
    QList<Touchpad> found;
    static const QRegularExpression event(R"(^event(\d+)$)");
    for (const QString &entry : QDir(sysRoot).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        if (!event.match(entry).hasMatch())
            continue;
        const QString dev = read(sysRoot + "/" + entry + "/dev").trimmed(); // "13:65"
        if (dev.isEmpty())
            continue;
        const QString udev = read(udevRoot + "/c" + dev);
        if (!udev.contains("E:ID_INPUT_TOUCHPAD=1"))
            continue;
        Touchpad pad;
        pad.path = "/dev/input/" + entry;
        pad.name = read(sysRoot + "/" + entry + "/device/name").trimmed();
        pad.hyprName = hyprName(pad.name);
        pad.internal = udev.contains("E:ID_INPUT_TOUCHPAD_INTEGRATION=internal");
        found.append(pad);
    }
    std::stable_sort(found.begin(), found.end(), [](const Touchpad &a, const Touchpad &b) {
        return a.internal && !b.internal;
    });
    return found;
}

}
