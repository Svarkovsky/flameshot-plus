# Flameshot +

Yes, yes, this is yet another fork of a fork.

I use Flameshot on a daily basis, but was frustrated by its limitations: missing window snapping, lack of native modern WebP output, and zero video/audio recording capability. So I took the codebase starting from commit [`70be63d478a271da549597d69bd4868607c0a395`](https://github.com/flameshot-org/flameshot/commit/70be63d478a271da549597d69bd4868607c0a395) (v12.1.0 release), extended it with the missing features, optimized the build, and had a lot of fun doing it.

If you like it, you can build it, use it, and enjoy a faster, fully featured tool. If this project helps you, feel free to support me!

---

## What is New in Flameshot +

- **Integrated Video & Audio Recording:** Record any selected screen area or snapped window directly into high-quality MP4 (`x264` + `AAC`) with perfect A/V synchronization (`aresample=async=1000`). Videos are saved automatically to `~/Video/flameshot-plus/`.
- **Pre-Record Floating HUD:** A dedicated floating toolbar (530px) that stays out of frame. Choose your audio mode before recording starts:
  - `System Audio`: Auto-detects active audio sink monitor, including Bluetooth headphones/speakers.
  - `Microphone`: Records system default input.
  - `System + Microphone`: Real-time mixed audio stream.
  - `Muted`: Silent video recording.
- **Window Snapping (X11):** Instant window geometry detection. Double-click any window (or press `W` / `Tab`) to automatically snap the selection frame to that exact application window.
- **Persistent Area Coordinates:** Selection geometry is automatically saved to `/tmp/screenink_area.env` and `/tmp/screenink_area.txt` for scripting and external tools.
- **Default WebP Format:** Ultra-lightweight screenshots saved directly in modern WebP format by default.
- **Maximum Build Optimization:** Fully compiled with `-O3`, `-flto=auto` (Link-Time Optimization), `-fno-plt`, `-fomit-frame-pointer`, and linker dead-code elimination (`-Wl,--gc-sections`). Resulting stripped binary is only **1.6 MB**.

---

## Quick Build & Run

### Prerequisites (Debian / Ubuntu)

```bash
sudo apt-get install build-essential cmake qtbase5-dev libqt5svg5-dev qttools5-dev-tools libx11-dev ffmpeg
```

### Build

```bash
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_IPO=ON

cmake --build build -j$(nproc)
```

### Run

```bash
./build/src/flameshot gui
```

### Shortcuts

- `W` or `Tab`: Snap selection to window under cursor
- Double Click: Snap selection to clicked window
- `Esc` or `Q`: Stop active recording / close overlay
- `Space` or `Enter`: Start recording when in ready mode

---

## Disclaimer & Upstream Authorship

All original rights, intellectual property, and authorship belong entirely to the original authors and contributors of Flameshot at [https://github.com/flameshot-org/flameshot](https://github.com/flameshot-org/flameshot).

This repository is an independent personal fork created solely for personal convenience. All upstream questions, feature requests, and bug reports regarding the base Flameshot application should be directed to the official project at [https://github.com/flameshot-org/flameshot](https://github.com/flameshot-org/flameshot).

The author of this fork accepts absolutely NO responsibility, liability, or warranty of any kind. I am not responsible for anything; use this software strictly at your own risk.

---

## Sponsor & Donate

If you enjoy Flameshot + and want to say thanks:
- **Sponsor / Donate:** [https://svarkovsky.github.io/donate/](https://svarkovsky.github.io/donate/)
- Support on GitHub: [@Svarkovsky](https://github.com/Svarkovsky)
