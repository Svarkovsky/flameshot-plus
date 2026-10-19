#!/usr/bin/env bash
set -e

BUILD_DIR="${1:-build}"
OUTPUT_DIR="${2:-.}"
PKG_NAME="flameshot-plus"
VERSION="12.1.0-1"
ARCH="amd64"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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
    strip -s -R .comment -R .note -R .note.gnu.build-id -R .note.ABI-tag "$STAGE_DIR/usr/bin/flameshot"
    if [ -x tools/sstrip ]; then
        tools/sstrip "$STAGE_DIR/usr/bin/flameshot" || true
    elif command -v sstrip >/dev/null 2>&1; then
        sstrip "$STAGE_DIR/usr/bin/flameshot" || true
    fi
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
Provides: flameshot
Conflicts: flameshot
Replaces: flameshot
Depends: libc6, libqt5core5a, libqt5gui5, libqt5widgets5, libqt5svg5, libqt5network5, libqt5dbus5, libx11-6, libxext6, libxfixes3, ffmpeg
Maintainer: Ivan Svarkovsky <https://github.com/Svarkovsky/flameshot-plus>
Homepage: https://github.com/Svarkovsky/flameshot-plus
Description: Powerful screen annotation, smart window snapping, and video recording
 Flameshot + is an extended and optimized fork of Flameshot.
 Adds live on-video drawing annotations, integrated desktop video/audio
 recording to MP4, smart window detection, and default WebP screenshots.
EOF

# Install copyright documentation
mkdir -p "$STAGE_DIR/usr/share/doc/$PKG_NAME"
if [ -f "$REPO_ROOT/LICENSE" ]; then
    cp "$REPO_ROOT/LICENSE" "$STAGE_DIR/usr/share/doc/$PKG_NAME/copyright"
elif [ -f LICENSE ]; then
    cp LICENSE "$STAGE_DIR/usr/share/doc/$PKG_NAME/copyright"
fi

# Add postinst and postrm triggers for desktop and icon caches
cat << 'EOF' > "$STAGE_DIR/DEBIAN/postinst"
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "$STAGE_DIR/DEBIAN/postinst"

cat << 'EOF' > "$STAGE_DIR/DEBIAN/postrm"
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "$STAGE_DIR/DEBIAN/postrm"

# Update desktop file name, exec path, and categories
if [ -f "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop" ]; then
    sed -i 's|^Exec=.*|Exec=/usr/bin/flameshot|' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
    sed -i 's/^Name=.*/Name=Flameshot +/' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
    sed -i 's/^Comment=.*/Comment=Screen annotation and video recording/' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
    sed -i 's/^Categories=.*/Categories=Graphics;Utility;2DGraphics;RasterGraphics;/' "$STAGE_DIR/usr/share/applications/org.flameshot.Flameshot.desktop"
    
fi

# Install pixmaps for legacy and lightweight window managers
mkdir -p "$STAGE_DIR/usr/share/pixmaps"
cp "$REPO_ROOT/data/img/hicolor/128x128/apps/flameshot.png" "$STAGE_DIR/usr/share/pixmaps/flameshot.png"
cp "$REPO_ROOT/data/img/hicolor/128x128/apps/flameshot.png" "$STAGE_DIR/usr/share/pixmaps/org.flameshot.Flameshot.png"
cp "$REPO_ROOT/data/img/hicolor/128x128/apps/flameshot.png" "$STAGE_DIR/usr/share/pixmaps/flameshot-plus.png"


echo "=== Building deb package ==="
mkdir -p "$OUTPUT_DIR"
DEB_FILE="$OUTPUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE_DIR" "$DEB_FILE"

echo "=== Package built successfully: $DEB_FILE ==="
dpkg -I "$DEB_FILE"
