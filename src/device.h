#pragma once

#include <QList>
#include <QString>

struct Touchpad {
    QString path;      // /dev/input/eventN
    QString name;      // kernel name, as libinput's MatchName sees it
    QString hyprName;  // Hyprland's device name
    bool internal = false;
};

namespace Device {

// Touchpads as udev tagged them, built-in ones first. Readable without root:
// names come from sysfs and the tags from udev's database.
QList<Touchpad> findTouchpads(const QString &sysRoot = "/sys/class/input",
                              const QString &udevRoot = "/run/udev/data");

// Hyprland lowercases device names and turns spaces into dashes.
QString hyprName(const QString &name);

}
