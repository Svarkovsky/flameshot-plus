#!/usr/bin/env bash
set -e

BUILD_DIR="${1:-build}"
OUTPUT_DIR="${2:-.}"
PKG_NAME="flameshot-plus"
VERSION="12.1.0-1"
ARCH="amd64"

STAGE_DIR=$(mktemp -d -t flameshot-deb-XXXXXX)
trap 'rm -rf "$STAGE_DIR"' EXIT

echo "=== Staging package installation into $STAGE_DIR ==="
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"

# Ensure binary is in /usr/bin with symlink flameshot-plus
if [ -d "$STAGE_DIR/usr/local" ]; then
    mkdir -p "$STAGE_DIR/usr/bin" "$STAGE_DIR/usr/share"
    if [ -d "$STAGE_DIR/usr/local/bin" ]; then
        cp -a "$STAGE_DIR/usr/local/bin/"* "$STAGE_DIR/usr/bin/"
    fi
    if [ -d "$STAGE_DIR/usr/local/share" ]; then
        cp -a "$STAGE_DIR/usr/local/share/"* "$STAGE_DIR/usr/share/"
    fi
    rm -rf "$STAGE_DIR/usr/local"
fi

if [ -f "$STAGE_DIR/usr/bin/flameshot" ]; then
    strip "$STAGE_DIR/usr/bin/flameshot"
    ln -sf flameshot "$STAGE_DIR/usr/bin/flameshot-plus"
fi

# Prepare DEBIAN/control
mkdir -p "$STAGE_DIR/DEBIAN"
cat << 'EOF' > "$STAGE_DIR/DEBIAN/control"
Package: flameshot-plus
Version: 12.1.0-1
Section: graphics
Priority: optional
Architecture: amd64
Depends: libc6, libqt5core5a, libqt5gui5, libqt5widgets5, libqt5svg5, libqt5network5, libqt5dbus5, libx11-6, ffmpeg
Maintainer: Svarkovsky <ivansvarkovsky@gmail.com>
Homepage: https://github.com/Svarkovsky/flameshot-plus
Description: Powerful screen annotation, smart window snapping, and video recording
 Flameshot + is an extended and optimized fork of Flameshot.
 Adds integrated desktop video/audio recording to MP4, smart window
 detection, persistent area coordinates, and default WebP screenshots.
EOF

# Update desktop file name and exec
if [ -f "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop" ]; then
    sed -i 's/^Name=.*/Name=Flameshot +/' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
    sed -i 's/^Comment=.*/Comment=Screen annotation and video recording/' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
fi

echo "=== Building deb package ==="
mkdir -p "$OUTPUT_DIR"
DEB_FILE="$OUTPUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE_DIR" "$DEB_FILE"

echo "=== Package built successfully: $DEB_FILE ==="
dpkg -I "$DEB_FILE"
