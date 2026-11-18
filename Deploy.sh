#!/usr/bin/env bash
# Builds MagicWallpaper and installs it for the current user (no sudo needed).
#   ./Deploy.sh                    build + install (GUI)
#   ./Deploy.sh --no-autostart     don't add it to startup applications
#   ./Deploy.sh --no-desktop-icon  don't put a shortcut on the Desktop
#   ./Deploy.sh --nogui            command-line only build (no GTK needed)
set -euo pipefail
cd "$(dirname "$0")"

AUTOSTART=1
DESKTOP_ICON=1
NOGUI=0
for arg in "$@"; do
    case "$arg" in
        --no-autostart)    AUTOSTART=0 ;;
        --no-desktop-icon) DESKTOP_ICON=0 ;;
        --nogui)           NOGUI=1 ;;
        *) echo "unknown option: $arg"; exit 1 ;;
    esac
done

missing=()
for bin in gcc make mpvpaper ffmpeg; do
    command -v "$bin" >/dev/null 2>&1 || missing+=("$bin")
done
if [ "$NOGUI" -eq 0 ]; then
    command -v pkg-config >/dev/null 2>&1 || missing+=("pkg-config")
    pkg-config --exists gtk4 2>/dev/null   || missing+=("gtk4 development files")
fi
if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing dependencies: ${missing[*]}"
    exit 1
fi

# ---------- build + install the executable ----------
make clean
if [ "$NOGUI" -eq 1 ]; then make nogui; else make; fi

BIN="$HOME/.local/bin/magicwallpaper"
install -Dm755 magicwallpaper "$BIN"
echo "Installed $BIN (make sure ~/.local/bin is in your PATH)"

if [ "$NOGUI" -eq 1 ]; then
    echo "Done (command line build, no icons/shortcuts created)."
    exit 0
fi

# ---------- icon ----------
ICON_DIR="$HOME/.local/share/icons/hicolor/scalable/apps"
install -Dm644 icon/magicwallpaper.svg "$ICON_DIR/magicwallpaper.svg"
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -q -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

# ---------- app menu entry ----------
APP_ENTRY="$HOME/.local/share/applications/magicwallpaper.desktop"
install -Dm755 /dev/stdin "$APP_ENTRY" <<DESK
[Desktop Entry]
Type=Application
Name=MagicWallpaper
Comment=Animated wallpapers with mpvpaper
Exec=$BIN
Icon=magicwallpaper
Categories=Settings;Utility;
Terminal=false
DESK
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
echo "App menu entry: $APP_ENTRY"

# ---------- Desktop shortcut ----------
if [ "$DESKTOP_ICON" -eq 1 ]; then
    DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
    if [ -d "$DESKTOP_DIR" ]; then
        cp "$APP_ENTRY" "$DESKTOP_DIR/magicwallpaper.desktop"
        chmod +x "$DESKTOP_DIR/magicwallpaper.desktop"
        # GNOME/Nautilus shows "Allow launching" until the file is marked trusted
        command -v gio >/dev/null 2>&1 && \
            gio set "$DESKTOP_DIR/magicwallpaper.desktop" metadata::trusted true 2>/dev/null || true
        echo "Desktop shortcut: $DESKTOP_DIR/magicwallpaper.desktop"
    else
        echo "No Desktop folder found, skipped the Desktop shortcut."
    fi
fi

# ---------- startup applications ----------
if [ "$AUTOSTART" -eq 1 ]; then
    install -Dm644 /dev/stdin "$HOME/.config/autostart/magicwallpaper-auto.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=MagicWallpaper (autostart)
Exec=$BIN -auto
Icon=magicwallpaper
Terminal=false
X-GNOME-Autostart-enabled=true
DESK
    echo "Startup entry: ~/.config/autostart/magicwallpaper-auto.desktop"
    echo "  Hyprland/Sway ignore this: add 'exec-once = $BIN -auto' (Hyprland) or 'exec $BIN -auto' (Sway) to your config."
fi

echo "Done. Open it from your app menu / Desktop icon, or run: magicwallpaper"
