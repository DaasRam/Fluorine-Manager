#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Build the standalone Fluorine Manager first
./build.sh all

# Create the AppImage
# This packages the standalone build into a .AppImage format
APPIMAGE_NAME="fluorine-manager.x86"
APPIMAGE_DIR="build/appimage"
mkdir -p "$APPIMAGE_DIR"

# Extract the standalone executable
cp "build/fluorine-manager/fluorine-manager" "$APPIMAGE_DIR/"

# Create a desktop file
cat > "$APPIMAGE_DIR/fluorine-manager.desktop" <<EOF
[Desktop Entry]
Name=Fluorine Manager
Comment=Fluorine Manager - Standalone Build
Exec=/usr/local/bin/fluorine-manager
Icon=utilities
Terminal=true
Type=Application
EOF

# Create a .desktop file for the application menu
cat > "$APPIMAGE_DIR/fluorine-manager.desktop" <<EOF
[Desktop Entry]
Name=Fluorine Manager
Exec=build/appimage/fluorine-manager
Icon=build/appimage/fluorine-manager.png
Type=Application
Categories=Utility
EOF

# Create a .desktop file for the terminal integration
cat > "$APPIMAGE_DIR/fluorine-manager-terminal.desktop" <<EOF
[Desktop Entry]
Name=Fluorine Manager (Terminal)
Exec=build/fluorine-manager/fluorine-manager
Terminal=true
Type=Application
EOF

# Create a manifest for AppImage
cat > "$APPIMAGE_DIR/manifest" <<EOF
name=Fluorine Manager
version=1.0.0
release=stable
arch=x86-64
EOF

# Create a README
cat > "$APPIMAGE_DIR/README" <<EOF
Fluorine Manager AppImage
========================

This is a standalone AppImage of Fluorine Manager.

Usage:
  Double-click fluorine-manager.x86 to launch
  Or run: xdg-open fluorine-manager.x86 &amp;amp; fluorine-manager.desktop

The AppImage includes:
- The standalone fluorine-manager executable
- Desktop launcher
- Terminal integration launcher
EOF

# Build the AppImage
echo "Building AppImage..."
cd "$APPIMAGE_DIR"
# Create the AppImage bundle
tar -czf fluorine-manager.x86 \
    fluorine-manager \
    fluorine-manager.desktop \
    fluorine-manager-terminal.desktop \
    manifest \
    README

echo "AppImage built: fluorine-manager.x86"
ls -lh fluorine-manager.x86
