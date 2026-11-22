#!/bin/sh
# Installs tenfold on Linux, the way that suits the system:
#
#   Arch and its kin (Omarchy, Arch Linux ARM on Asahi)
#       the release's pacman package, which brings Qt 6 and PipeWire with it
#   NixOS
#       the flake in the repository, through nix profile
#   any other Linux (Ubuntu, Debian, Fedora, openSUSE, Mint, Pop!_OS, ...)
#       the release's bundle, which carries its own Qt, into ~/.local; the
#       system's package manager adds PipeWire's library and polkit if
#       they're missing
#
# Each download is checked against its checksum. Nothing else on the system
# changes: tenfold asks for your password itself when it reads the trackpad.
#
#   curl -fsSL https://maxberggren.github.io/tenfold/install.sh | sh
#   curl -fsSL https://maxberggren.github.io/tenfold/install.sh | sh -s -- --uninstall
#
# TENFOLD_METHOD=pacman|nix|bundle picks the way by hand, and TENFOLD_BUNDLE
# installs a bundle already on disk instead of the latest release's.
#
# Read before running: https://github.com/maxberggren/tenfold/blob/master/site/install.sh
set -eu

repo=maxberggren/tenfold

say() { printf '%s\n' "$*"; }
fail() {
    printf 'tenfold: %s\n' "$*" >&2
    exit 1
}
have() { command -v "$1" >/dev/null 2>&1; }

# Root needs no sudo (containers, root shells); everyone else does.
if [ "$(id -u)" = 0 ]; then
    sudo=""
elif have sudo; then
    sudo="sudo"
elif have doas; then
    sudo="doas"
else
    sudo="sudo"
fi

# Under `curl | sh` this script is stdin, so anything that asks a question
# (pacman, apt) must read the answer from the terminal, and with no
# terminal at all it must not ask.
if (: </dev/tty) 2>/dev/null; then
    interactive=1
else
    interactive=""
fi
ask() {
    if [ -n "$interactive" ]; then
        "$@" </dev/tty
    else
        "$@" </dev/null
    fi
}

# ---------------------------------------------------------------- what's here

[ "$(uname -s)" = Linux ] || fail "tenfold runs on Linux for now. macOS and Windows are coming: https://github.com/$repo"
have curl || fail "curl is needed to download tenfold"

# The bundle needs a glibc at least as new as the system it was built on:
# Debian 12 for x86_64, Ubuntu 24.04 for arm64 (where Qt's own build needs it).
case "$(uname -m)" in
x86_64 | amd64) arch=x86_64 min_glibc=2.36 oldest="Ubuntu 23.04, Debian 12, Fedora 37" ;;
aarch64 | arm64) arch=aarch64 min_glibc=2.39 oldest="Ubuntu 24.04, Debian 13, Fedora 40" ;;
*) fail "no tenfold for $(uname -m) yet (x86_64 and arm64 only)" ;;
esac

os_id=""
os_name="Linux"
if [ -r /etc/os-release ]; then
    os_id=$(. /etc/os-release && printf '%s' "${ID:-}")
    os_name=$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-${NAME:-Linux}}")
fi

method="${TENFOLD_METHOD:-}"
if [ -z "$method" ]; then
    if [ "$os_id" = nixos ] || [ -e /etc/NIXOS ]; then
        method=nix
    elif have pacman && [ -z "${TENFOLD_BUNDLE:-}" ]; then
        method=pacman
    else
        method=bundle
    fi
fi

# The bundle's own folder: program files only. What tenfold keeps for you
# lives elsewhere (~/.local/state/tenfold, your Music folder).
share="${XDG_DATA_HOME:-$HOME/.local/share}"
bindir="$HOME/.local/bin"
home="$HOME/.local/lib/tenfold"

# -------------------------------------------------------------------- helpers

# The newest release's asset whose file name matches the pattern (an
# extended regular expression).
latest_asset() {
    curl -fsSL "https://api.github.com/repos/$repo/releases/latest" |
        grep -o "\"browser_download_url\": *\"[^\"]*\"" |
        sed 's/.*"\(https[^"]*\)"$/\1/' |
        grep -E "/$1\$" | head -n 1
}

# Downloads url into dir and checks it against the release's checksum.
fetch_checked() {
    url=$1
    dir=$2
    file="$dir/${url##*/}"
    say "Downloading ${url##*/}"
    curl -fL --progress-bar -o "$file" "$url"
    curl -fsSL -o "$file.sha256" "$url.sha256" || fail "the release has no checksum for ${url##*/}"
    (cd "$dir" && sha256sum -c --quiet "${file##*/}.sha256") ||
        fail "the download doesn't match its checksum; nothing was installed"
}

has_lib() {
    if have ldconfig && ldconfig -p 2>/dev/null | grep -q "$1"; then
        return 0
    fi
    for dir in /usr/lib64 /usr/lib /lib64 /lib /usr/lib/"$arch"-linux-gnu /lib/"$arch"-linux-gnu; do
        [ -e "$dir/$1" ] && return 0
    done
    return 1
}

# The package that provides what is missing, in this system's package
# manager's words: pipewire (its client library) or pkexec.
package_for() {
    case "$pm:$1" in
    apt:pipewire)
        if apt-cache show libpipewire-0.3-0t64 >/dev/null 2>&1; then echo libpipewire-0.3-0t64; else echo libpipewire-0.3-0; fi ;;
    apt:pkexec)
        if apt-cache show pkexec >/dev/null 2>&1; then echo pkexec; else echo policykit-1; fi ;;
    dnf:pipewire) echo pipewire-libs ;;
    zypper:pipewire) echo libpipewire-0_3-0 ;;
    xbps:pipewire) echo libpipewire ;;
    *:pkexec) echo polkit ;;
    esac
}

install_packages() {
    case "$pm" in
    apt)
        $sudo apt-get update -qq </dev/null || true
        if [ -n "$interactive" ]; then ask $sudo apt-get install "$@"; else $sudo apt-get install -y "$@" </dev/null; fi ;;
    dnf)
        if [ -n "$interactive" ]; then ask $sudo dnf install "$@"; else $sudo dnf install -y "$@" </dev/null; fi ;;
    zypper)
        if [ -n "$interactive" ]; then ask $sudo zypper install "$@"; else $sudo zypper --non-interactive install "$@" </dev/null; fi ;;
    xbps)
        if [ -n "$interactive" ]; then ask $sudo xbps-install "$@"; else $sudo xbps-install -y "$@" </dev/null; fi ;;
    *) return 1 ;;
    esac
}

# ------------------------------------------------------------------ uninstall

uninstall() {
    removed=""
    if [ -e "$home" ] || [ -L "$bindir/tenfold" ]; then
        rm -rf "$home"
        if [ -L "$bindir/tenfold" ]; then rm -f "$bindir/tenfold"; fi
        rm -f "$share/applications/tenfold.desktop" "$share/icons/hicolor/scalable/apps/tenfold.svg"
        if have update-desktop-database; then update-desktop-database "$share/applications" 2>/dev/null || true; fi
        say "Removed tenfold from $home."
        removed=1
    fi
    # The package or the Nix profile only when that's how this system
    # installs it (TENFOLD_METHOD=bundle leaves them alone).
    if [ "$method" = pacman ] && pacman -Q tenfold >/dev/null 2>&1; then
        say "Removing the tenfold package (pacman asks for your password)…"
        ask $sudo pacman -R tenfold
        removed=1
    fi
    if [ "$method" = nix ] && have nix && nix --extra-experimental-features "nix-command flakes" profile list 2>/dev/null | grep -q tenfold; then
        nix --extra-experimental-features "nix-command flakes" profile remove tenfold
        removed=1
    fi
    if [ -n "$removed" ]; then
        say "Your songs and recordings stay where you saved them, and the last song in ~/.local/state/tenfold."
    else
        say "tenfold isn't installed here."
    fi
    exit 0
}

case "${1:-}" in
--uninstall | uninstall) uninstall ;;
"") ;;
*) fail "unknown option $1 (the only one is --uninstall)" ;;
esac

# ---------------------------------------------------------------------- pacman

install_pacman() {
    say "Finding the latest tenfold package for $arch…"
    url=$(latest_asset "tenfold-[0-9][^/]*-$arch\.pkg\.tar\.zst") || true
    [ -n "$url" ] || fail "no package for $arch yet: https://github.com/$repo/releases"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    fetch_checked "$url" "$tmp"
    say "Installing with pacman (it asks for your password)…"
    if [ -n "$interactive" ]; then
        ask $sudo pacman -U --needed "$tmp/${url##*/}"
    else
        $sudo pacman -U --needed --noconfirm "$tmp/${url##*/}" </dev/null
    fi
    say ""
    say "tenfold is installed. Open it from the app launcher, or run: tenfold"
}

# ------------------------------------------------------------------------- nix

install_nix() {
    have nix || fail "Nix isn't on the PATH. On NixOS, add tenfold to your configuration instead: https://github.com/$repo/blob/master/flake.nix"
    say "Installing tenfold with Nix (it builds against nixpkgs' Qt, which takes a few minutes the first time)…"
    nix --extra-experimental-features "nix-command flakes" profile install "github:$repo" ||
        fail "nix profile couldn't install it. Try it without installing: nix run github:$repo"
    say ""
    say "tenfold is installed. Open it from the app launcher, or run: tenfold"
    say "It asks for your password to read the trackpad, through polkit (security.polkit.enable)."
    say "To skip that, add the package to services.udev.packages: see flake.nix in the repository."
}

# ---------------------------------------------------------------------- bundle

install_bundle() {
    # The bundle carries Qt but not the C library, which must be glibc and
    # no older than the one it was built against.
    glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | sed -n 's/^glibc //p') || true
    [ -n "$glibc" ] || fail "this system's C library isn't glibc (Alpine and other musl systems aren't supported yet)"
    if [ "$(printf '%s\n%s\n' "$min_glibc" "$glibc" | sort -t. -k1,1n -k2,2n | head -n 1)" != "$min_glibc" ]; then
        fail "$os_name has glibc $glibc; tenfold needs $min_glibc or newer ($oldest or later)"
    fi

    pm=""
    if have apt-get; then pm=apt
    elif have dnf; then pm=dnf
    elif have zypper; then pm=zypper
    elif have xbps-install; then pm=xbps
    fi

    # What the bundle takes from the system that a desktop may still lack:
    # PipeWire's client library (it must match the sound server, so it isn't
    # bundled) and pkexec, which asks for the password to read the pad.
    missing=""
    has_lib libpipewire-0.3.so.0 || missing="pipewire"
    have pkexec || missing="$missing pkexec"
    if [ -n "$missing" ]; then
        packages=""
        if [ -n "$pm" ]; then
            for need in $missing; do packages="$packages $(package_for "$need")"; done
        fi
        if [ -n "$packages" ]; then
            say "tenfold needs$packages from $os_name; installing (it may ask for your password)…"
            # shellcheck disable=SC2086
            install_packages $packages || fail "couldn't install$packages. Install them, then run this again."
        else
            fail "tenfold needs PipeWire's library (libpipewire-0.3.so.0) and polkit's pkexec. Install them with your package manager, then run this again."
        fi
    fi

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    if [ -n "${TENFOLD_BUNDLE:-}" ]; then
        tarball="$TENFOLD_BUNDLE"
        [ -r "$tarball" ] || fail "can't read $tarball"
    else
        say "Finding the latest tenfold for $arch…"
        url=$(latest_asset "tenfold-[0-9][^/]*-$arch-linux\.tar\.gz") || true
        [ -n "$url" ] || fail "no Linux bundle for $arch yet: https://github.com/$repo/releases"
        fetch_checked "$url" "$tmp"
        tarball="$tmp/${url##*/}"
    fi

    # Unpacked beside the old one and swapped in, so a failed download or a
    # full disk never leaves half an install.
    mkdir -p "${home%/*}" "$bindir"
    rm -rf "$home.new"
    mkdir -p "$home.new"
    tar -xzf "$tarball" -C "$home.new" --strip-components=1 || fail "couldn't unpack $tarball"
    [ -x "$home.new/usr/bin/tenfold" ] || fail "$tarball isn't a tenfold bundle"
    rm -rf "$home.old"
    if [ -e "$home" ]; then mv "$home" "$home.old"; fi
    mv "$home.new" "$home"
    rm -rf "$home.old"

    ln -sf "$home/usr/bin/tenfold" "$bindir/tenfold"
    # The launcher entry names the binary by its full path: launchers don't
    # all have ~/.local/bin on their PATH.
    mkdir -p "$share/applications" "$share/icons/hicolor/scalable/apps"
    sed "s|^Exec=tenfold|Exec=$home/usr/bin/tenfold|" "$home/tenfold.desktop" >"$share/applications/tenfold.desktop"
    cp "$home/tenfold.svg" "$share/icons/hicolor/scalable/apps/tenfold.svg"
    if have update-desktop-database; then update-desktop-database "$share/applications" 2>/dev/null || true; fi

    # Proof it runs here: every library found, Qt and all.
    if ! "$home/usr/bin/tenfold" --help >/dev/null 2>&1; then
        say "tenfold is unpacked in $home but doesn't start on $os_name:" >&2
        ldd "$home/usr/bin/tenfold" 2>/dev/null | grep 'not found' >&2 || "$home/usr/bin/tenfold" --help >&2 || true
        fail "install the libraries named above, then run this again"
    fi

    say ""
    say "tenfold $(cat "$home/VERSION" 2>/dev/null || true) is installed in $home."
    case ":$PATH:" in
    *":$bindir:"*) say "Open it from the app launcher, or run: tenfold" ;;
    *) say "Open it from the app launcher, or run: $bindir/tenfold (~/.local/bin isn't on your PATH)" ;;
    esac
    say "To remove it: curl -fsSL https://maxberggren.github.io/tenfold/install.sh | sh -s -- --uninstall"
}

case "$method" in
pacman) install_pacman ;;
nix) install_nix ;;
bundle) install_bundle ;;
*) fail "TENFOLD_METHOD is pacman, nix or bundle, not $method" ;;
esac
say "Take the trackpad with the Take pad button, play, and press ? on any page."
