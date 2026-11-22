#pragma once

// Open and Save dialogs through xdg-desktop-portal's FileChooser over
// D-Bus, so the desktop's own picker shows (never QFileDialog). Each request
// gets a handle token of its own; the portal answers on a Request object at
// a path made from that token, which is listened to before the call is made
// so a quick answer is never missed.

#include <QObject>
#include <QVariantMap>
#include <functional>

#include "songfile.h"

// One dialog the portal shows: it answers once, on its own Request path.
class PortalRequest : public QObject {
    Q_OBJECT
public:
    PortalRequest(std::function<void(const QString &)> done, QObject *parent) : QObject(parent), m_done(std::move(done)) {}
    void finish(const QString &path);

public slots:
    void response(uint code, const QVariantMap &results);

private:
    std::function<void(const QString &)> m_done;
};

class PortalFilePicker : public QObject, public tf::FilePicker {
    Q_OBJECT
public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openFile(const QString &title, std::function<void(const QString &)> done) override;
    void saveFile(const QString &title, const QString &name, const QString &folder,
                  std::function<void(const QString &)> done) override;

    // Said when no dialog can be shown (no portal, the call refused), since
    // to the player that would look like a key that does nothing.
    std::function<void(const QString &)> onError;

private:
    void ask(const QString &method, const QString &title, QVariantMap options,
             std::function<void(const QString &)> done);

    int m_next = 0;
};
