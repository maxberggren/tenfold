#pragma once

namespace tf {

// Whether macOS lets tenfold use the microphone (Privacy & Security), from
// AVFoundation. Denied: the player said no, or a policy did.
bool microphoneDenied();
// Shows the system's question if it hasn't been asked yet; returns at once.
void askForMicrophone();

}
