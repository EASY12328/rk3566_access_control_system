#!/bin/sh
# The build script installs this launcher beside access_preview as run.sh.
set -eu
cd "$(dirname "$0")"

# Match the installed Weston socket without changing system display settings.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export QT_QPA_PLATFORM=wayland
if [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; then
    echo "Weston socket missing: $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" >&2
    exit 1
fi
exec ./access_preview "$@"
