#pragma once

#include "songfile.h"

namespace tf {

// The Mac's own Open and Save panels, each shown on its own and answering
// once when the player is done with it.
class MacFilePicker : public FilePicker {
public:
    void openFile(const QString &title, std::function<void(const QString &)> done) override;
    void saveFile(const QString &title, const QString &name, const QString &folder,
                  std::function<void(const QString &)> done) override;
};

}
