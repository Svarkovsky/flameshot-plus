#!/usr/bin/env bash
set -e

BIN_PATH="$1"
if [ -z "$BIN_PATH" ]; then
    BIN_PATH="./flameshot-plus"
fi

echo "=== Running Validation on $BIN_PATH ==="

# 1. Check binary existence and executable bit
test -x "$BIN_PATH"
echo "[PASS] Binary exists and is executable"

# 2. Check --version
VERSION_OUT=$("$BIN_PATH" --version)
echo "$VERSION_OUT" | grep -q "Flameshot"
echo "[PASS] Version check: $VERSION_OUT"

# 3. Check --help
"$BIN_PATH" --help > /dev/null
echo "[PASS] Help command runs cleanly"

# 4. Check screenshot creation in WebP
TEST_WEBP="/tmp/test_val_screen.webp"
rm -f "$TEST_WEBP"
"$BIN_PATH" screen -p "$TEST_WEBP"
test -f "$TEST_WEBP"
file "$TEST_WEBP" | grep -q "Web/P"
echo "[PASS] Screen capture to WebP works ($(stat -c%s "$TEST_WEBP") bytes)"
rm -f "$TEST_WEBP"

# 5. Check video directory creation
test -d "$HOME/Video/flameshot-plus"
echo "[PASS] Video directory exists: $HOME/Video/flameshot-plus"

# 6. Check size
BIN_SIZE=$(stat -c%s "$BIN_PATH")
echo "[INFO] Final binary size: $BIN_SIZE bytes ($(( BIN_SIZE / 1024 )) KB)"

echo "=== ALL VALIDATION CHECKS PASSED ==="
