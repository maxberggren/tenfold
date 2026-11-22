#!/bin/sh
# Installs tenfold: the latest release's package for this machine (x86_64 or
# arm64), checked against its checksum and installed with pacman, which also
# installs what it needs (Qt 6, PipeWire, polkit). The package adds a udev
# rule so the person at the computer can read the trackpad without a
# password; nothing else on the system changes.
#
#   curl -fsSL https://maxberggren.github.io/tenfold/install.sh | sh
#
# Read before running: https://github.com/maxberggren/tenfold/blob/master/site/install.sh
set -eu

repo=maxberggren/tenfold

say() { printf '%s\n' "$*"; }
fail() {
    printf 'tenfold: %s\n' "$*" >&2
    exit 1
}

[ "$(uname -s)" = Linux ] || fail "tenfold runs on Linux for now. macOS and Windows are coming: https://github.com/$repo"
command -v pacman >/dev/null 2>&1 ||
    fail "this installer is for Arch-based systems (Omarchy, Arch, Arch Linux ARM on Asahi). Elsewhere, build it from source: https://github.com/$repo#how-its-built"
command -v curl >/dev/null 2>&1 || fail "curl is needed: sudo pacman -S curl"

case "$(uname -m)" in
x86_64) arch=x86_64 ;;
aarch64 | arm64) arch=aarch64 ;;
*) fail "no package for $(uname -m) yet (x86_64 and arm64 only)" ;;
esac

say "Finding the latest tenfold for $arch…"
url=$(curl -fsSL "https://api.github.com/repos/$repo/releases/latest" |
    grep -o "\"browser_download_url\": *\"[^\"]*/tenfold-[0-9][^/\"]*-$arch\.pkg\.tar\.zst\"" |
    sed 's/.*"\(https[^"]*\)"$/\1/' | head -n 1) || true
[ -n "$url" ] || fail "no release for $arch yet: https://github.com/$repo/releases"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pkg="$tmp/${url##*/}"

say "Downloading ${url##*/}"
curl -fL --progress-bar -o "$pkg" "$url"
curl -fsSL -o "$pkg.sha256" "$url.sha256" || fail "the release has no checksum for ${url##*/}"
(cd "$tmp" && sha256sum -c --quiet "${pkg##*/}.sha256") || fail "the download doesn't match its checksum; nothing was installed"

say "Installing (pacman asks for your password)…"
sudo pacman -U --needed "$pkg"

say ""
say "tenfold is installed. Open it from the app launcher, or run: tenfold"
say "Take the trackpad with the Take pad button, play, and press ? on any page."
