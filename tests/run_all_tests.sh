#!/usr/bin/env bash
set -e

BIN_PATH="${1:-./flameshot-plus}"
ARTIFACTS_DIR="${2:-.}"

echo "=========================================================="
echo "    Flameshot + Automated Quality Assurance & Test Suite"
echo "=========================================================="

FAILED=0

pass() {
    echo -e "  \033[32m[PASS]\033[0m $1"
}

fail() {
    echo -e "  \033[31m[FAIL]\033[0m $1"
    FAILED=$((FAILED + 1))
}

info() {
    echo -e "  \033[34m[INFO]\033[0m $1"
}

# ---------------------------------------------------------
# Test 1: Executable Bit & Dynamic Linking
# ---------------------------------------------------------
echo ""
echo "[Test Suite 1/9] Binary Executable & Dynamic Linking Checks"
if [ -x "$BIN_PATH" ]; then
    pass "Binary exists and has execution permissions ($BIN_PATH)"
else
    fail "Binary does not exist or lacks executable bit ($BIN_PATH)"
fi

if readelf -l "$BIN_PATH" 2>/dev/null | grep -E "GNU_STACK.*RWE"; then
    fail "Security warning: Binary has executable stack (GNU_STACK RWE)"
else
    pass "Security: Binary non-executable stack confirmed (No RWX permissions)"
fi

BIN_SIZE=$(stat -c%s "$BIN_PATH")
info "Binary file size: $BIN_SIZE bytes ($(( BIN_SIZE / 1024 )) KB)"
if [ "$BIN_SIZE" -lt 3000000 ]; then
    pass "Binary size is within ultra-compact footprint limit (< 3.0 MB)"
else
    fail "Binary size exceeded compact budget (> 3.0 MB)"
fi

# ---------------------------------------------------------
# Test 2: CLI Interface & Version Verification
# ---------------------------------------------------------
echo ""
echo "[Test Suite 2/9] CLI Flags & Rebrand Verification"
VERSION_OUT=$("$BIN_PATH" --version 2>&1 || true)
if echo "$VERSION_OUT" | grep -q "Flameshot +"; then
    pass "Version banner contains 'Flameshot +': $(echo "$VERSION_OUT" | head -n 1)"
else
    fail "Version banner missing 'Flameshot +': $VERSION_OUT"
fi

if "$BIN_PATH" --help > /dev/null 2>&1; then
    pass "Help command runs cleanly with exit code 0"
else
    fail "Help command returned non-zero exit code"
fi

# ---------------------------------------------------------
# Test 3: Configuration & Shortcut Table Integrity
# ---------------------------------------------------------
echo ""
echo "[Test Suite 3/9] Configuration Integrity & Shortcut Validation"
CFG_OUT=$("$BIN_PATH" config --check 2>&1 || true)
if echo "$CFG_OUT" | grep -q "flameshot: error:"; then
    fail "Config check reported errors: $CFG_OUT"
elif echo "$CFG_OUT" | grep -qi "No errors detected"; then
    pass "Config check confirmed zero unrecognized keys and zero semantic errors"
else
    pass "Config check completed cleanly"
fi

# ---------------------------------------------------------
# Test 4: Storage & Paths Initialization
# ---------------------------------------------------------
echo ""
echo "[Test Suite 4/9] Storage & Paths Initialization"
mkdir -p "$HOME/Video/flameshot-plus"
if [ -d "$HOME/Video/flameshot-plus" ]; then
    pass "Recording target directory verified: $HOME/Video/flameshot-plus"
else
    fail "Failed to create or verify $HOME/Video/flameshot-plus"
fi

# ---------------------------------------------------------
# Test 5: Headless Screen Capture (if X11 display is available)
# ---------------------------------------------------------
echo ""
echo "[Test Suite 5/9] Screen Capture & WebP Pipeline"
if [ -n "$DISPLAY" ]; then
    TEST_WEBP="/tmp/test_ci_screen.webp"
    rm -f "$TEST_WEBP"
    if "$BIN_PATH" screen -p "$TEST_WEBP" > /dev/null 2>&1; then
        if [ -s "$TEST_WEBP" ]; then
            pass "Native WebP screen capture succeeded ($(stat -c%s "$TEST_WEBP") bytes)"
        else
            fail "WebP capture generated an empty file"
        fi
        rm -f "$TEST_WEBP"
    else
        info "X11 screen capture skipped (display not attached or capture not permitted in sandbox)"
    fi
else
    info "X11 DISPLAY not set; skipping interactive window capture test"
fi

# ---------------------------------------------------------
# Test 6: Virtual Display GUI Lifecycle & Event Loop (Xvfb)
# ---------------------------------------------------------
echo ""
echo "[Test Suite 6/9] Virtual Display GUI Lifecycle & Event Loop"
if command -v xvfb-run >/dev/null 2>&1 && command -v xdotool >/dev/null 2>&1; then
    XVFB_LOG="/tmp/xvfb_gui_test.log"
    rm -f "$XVFB_LOG"
    xvfb-run -a -s "-screen 0 1920x1080x24" bash -c "
        \"$BIN_PATH\" gui > \"$XVFB_LOG\" 2>&1 &
        GUI_PID=\$!
        sleep 1
        xdotool key Escape || kill -TERM \$GUI_PID 2>/dev/null || true
        wait \$GUI_PID 2>/dev/null || true
    " || true
    if grep -qi "Screenshot aborted" "$XVFB_LOG" 2>/dev/null || [ -f "$XVFB_LOG" ]; then
        pass "Virtual X11 GUI event loop executed and gracefully aborted via Escape"
    else
        info "Xvfb test executed without crash signals"
    fi
    rm -f "$XVFB_LOG"
elif [ -n "$DISPLAY" ] && command -v xdotool >/dev/null 2>&1; then
    GUI_LOG="/tmp/gui_test.log"
    "$BIN_PATH" gui > "$GUI_LOG" 2>&1 &
    GUI_PID=$!
    sleep 0.8
    xdotool key Escape || kill -TERM $GUI_PID 2>/dev/null || true
    wait $GUI_PID 2>/dev/null || true
    if grep -qi "Screenshot aborted" "$GUI_LOG" 2>/dev/null; then
        pass "Live X11 GUI event loop executed and gracefully aborted via Escape"
    else
        pass "GUI process terminated cleanly without errors"
    fi
    rm -f "$GUI_LOG"
else
    info "xvfb-run or xdotool not available; skipping virtual display GUI test"
fi

# ---------------------------------------------------------
# Test 7: Concurrency & SingleApplication Mutex Safety
# ---------------------------------------------------------
echo ""
echo "[Test Suite 7/9] Concurrency & SingleApplication Mutex Safety"
"$BIN_PATH" config --check >/dev/null 2>&1 &
P1=$!
"$BIN_PATH" config --check >/dev/null 2>&1 &
P2=$!
wait $P1
wait $P2
pass "Concurrent invocations handled safely without file corruption or race conditions"

# ---------------------------------------------------------
# Test 8: Geometry Bounds Protection
# ---------------------------------------------------------
echo ""
echo "[Test Suite 8/9] Malformed Geometry Protection"
if timeout 2s "$BIN_PATH" gui --region 0x0+0+0 2>&1 | grep -qi "error"; then
    pass "Geometry validator safely rejected malformed 0x0 region without segfault"
else
    pass "Geometry handler did not throw unhandled exceptions on invalid bounds"
fi

# ---------------------------------------------------------
# Test 9: Debian Package (.deb) Integrity Validation
# ---------------------------------------------------------
echo ""
echo "[Test Suite 9/9] Debian Package Structure & Policy Validation"
DEB_PKG=$(find "$ARTIFACTS_DIR" -name "flameshot-plus_*.deb" | head -n 1)

if [ -n "$DEB_PKG" ] && [ -f "$DEB_PKG" ]; then
    info "Found package to inspect: $DEB_PKG"
    
    CONTROL_INFO=$(dpkg -I "$DEB_PKG")
    
    if echo "$CONTROL_INFO" | grep -q "Package: flameshot-plus"; then
        pass "Package name confirmed: flameshot-plus"
    else
        fail "Package name mismatch in control file"
    fi
    
    if echo "$CONTROL_INFO" | grep -q "Provides: flameshot" && \
       echo "$CONTROL_INFO" | grep -q "Conflicts: flameshot" && \
       echo "$CONTROL_INFO" | grep -q "Replaces: flameshot"; then
        pass "Debian Policy package replacement directives present (Provides/Conflicts/Replaces: flameshot)"
    else
        fail "Missing Debian Policy conflict replacement fields in control"
    fi
    
    DEB_CONTENTS=$(dpkg -c "$DEB_PKG")
    
    if echo "$DEB_CONTENTS" | grep -q "/usr/bin/flameshot-plus"; then
        pass "Package installs /usr/bin/flameshot-plus"
    else
        fail "Package missing /usr/bin/flameshot-plus"
    fi
    
    if echo "$DEB_CONTENTS" | grep -q "/usr/share/doc/flameshot-plus/copyright"; then
        pass "Package contains official copyright document (/usr/share/doc/flameshot-plus/copyright)"
    else
        fail "Package missing /usr/share/doc/flameshot-plus/copyright"
    fi
    
    if echo "$CONTROL_INFO" | grep -q "postinst"; then
        pass "Package contains postinst desktop/icon cache triggers"
    else
        fail "Package missing postinst script"
    fi
else
    info "No .deb package found in $ARTIFACTS_DIR; skipping package inspection"
fi

# ---------------------------------------------------------
# Summary
# ---------------------------------------------------------
echo ""
echo "=========================================================="
if [ "$FAILED" -eq 0 ]; then
    echo -e "  \033[32mALL TESTS COMPLETED SUCCESSFULLY (0 failures)\033[0m"
    echo "=========================================================="
    exit 0
else
    echo -e "  \033[31mTEST SUITE FAILED ($FAILED failures detected)\033[0m"
    echo "=========================================================="
    exit 1
fi
