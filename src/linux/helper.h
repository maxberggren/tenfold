#pragma once

// `tenfold helper /dev/input/eventN`, run as root through pkexec (only root
// and the input group can read a touchpad). It prints the pad's ranges once,
// then one line per hardware frame, and grabs the pad on request so the
// fingers play tenfold instead of moving the pointer:
//
//   info <xmin> <xmax> <ymin> <ymax> <majormax> <xres> <yres> <omin> <omax> <pmin> <pmax> 0 <name>
//   f <microseconds> <button> [<id>,<x>,<y>,<major>,<minor>,<orientation>,<pressure> ...]
//   ok grab | ok ungrab | err <what> <message>
//
// It reads `grab` and `ungrab` on stdin and exits when stdin closes, so it
// never outlives the app and the kernel drops the grab with it.
int runHelper(const char *devicePath);
