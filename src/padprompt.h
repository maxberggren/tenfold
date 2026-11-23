#pragma once

#include "platform.h"

namespace tf {

// Whether the window puts up the box that asks for the trackpad: only when
// the player has something to do about it. A pad that's missing or failed
// says why; one never taken, or given back, waits for the button.
//
// away: the pad was put down for the player because tenfold stopped being
// the window in front (with focus following the mouse, just the pointer
// crossing onto another window), and it's taken again when tenfold is back
// in front. Nothing to do there, so no box: it would come up each time the
// pointer leaves. Unless coming back asks for the password, or fails.
inline bool padPrompt(bool found, Pad::Status status, bool away, bool needsPermission) {
    if (!found || status == Pad::Failed)
        return true;
    if (status == Pad::Grabbed)
        return false;
    if (away)
        return status == Pad::Starting && needsPermission;
    return true;
}

}
