# Aliens-inspired sentry display

A portable LVGL recreation of a movie-style remote sentry screen, with a desktop
simulator and firmware for the **ESP32-2432S028R Cheap Yellow Display (CYD)**.

Numeric readouts are real text, not a background screenshot. The warning sound is
synthesized on-device from sine-wave harmonics. Both targets share the same UI,
bitmap fonts, and audio synthesis code.

This is an independent fan project, not affiliated with or endorsed by the film's
rights holders. See [NOTICE.md](NOTICE.md) for licensing scope. It is a display/demo,
not a safety-critical alarm or weapon controller.

## Features

- Rounds, independent time readout, temperature/range gauges, A/B station badges.
- Configurable CRITICAL threshold and blink interval.
- Sample-clocked audio on the CYD speaker output; visuals follow DAC playback.
- Tap anywhere on the CYD touchscreen to silence the current alarm. The visual
  warning continues, and sound re-arms for the next critical episode.
- Desktop state controls, headless captures, WAV synthesis, and regression tests.

## Build the desktop simulator

Requirements: C compiler, CMake 3.22+, Git, pkg-config, SDL2 development files.
Linux is the tested desktop platform. The desktop build does not require ESP-IDF.

For example, on Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake git pkg-config libsdl2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/aliens
```

CMake fetches LVGL **v9.3.0**. For an offline build, supply that release in
`external/lvgl/` and configure with `-DALIENS_FETCH_LVGL=OFF`. Otherwise the first
configure requires network access. The UI's bitmap fonts are included as C data.

### Desktop controls

| Key | Action |
|---|---|
| Up / Down | Increase / decrease rounds |
| Left / Right | Adjust time independently |
| Shift | Larger adjustment steps |
| U | Restore automatic time from rounds |
| T / G | Increase / decrease temperature |
| R / F | Increase / decrease range |
| A / B | Select station |
| C | Toggle forced CRITICAL |
| Space | Start / stop demo |
| L | Toggle lamp test |
| Q | Quit |

Run `./build/aliens --help` for command-line options. Examples:

```sh
./build/aliens --rounds 10 --time 0.67 --temp 88 --range 16 --station B
./build/aliens --capture build/critical.ppm --rounds 10 --capture-ms 171
./build/aliens --rounds 25 --critical-threshold 30 --blink-ms 250
./build/aliens_bell --wav build/warning.wav --volume 15
```

The SDL simulator is silent. Open `build/warning.wav` in a player to audition the
same C synthesizer used on the board. `aliens_gauge` is an optional gauge-isolation
debug target. Headless capture/tests do not need an attached display.

## Build and flash the CYD

Target: classic ESP32 **ESP32-2432S028R**, ILI9341 320×240 LCD and XPT2046 resistive
touch. Other CYD variants may have different wiring or displays and are not
interchangeable without changes.

Install [ESP-IDF v6.0.3](https://docs.espressif.com/projects/esp-idf/en/v6.0.3/esp32/get-started/)
with the ESP32 tools, then activate its environment following Espressif's setup
instructions. Use an isolated supported Python environment. This project does
not install an SDK, change your shell startup, or require PlatformIO.

From this repository's root, in an activated ESP-IDF shell:

```sh
FW="$PWD/firmware/esp32-2432S028R"
idf.py -C "$FW" -B "$FW/build" build
idf.py -C "$FW" -B "$FW/build" -p /dev/ttyUSB0 flash monitor
```

Select your actual serial port (`/dev/ttyUSB0` is only a Linux example). Building
requires network access on the first run for managed dependencies. The committed
`dependencies.lock` records tested component versions. The root CMake project is
for desktop; **do not run `idf.py` against the repository root**.

Exit the serial monitor with **Ctrl+]**. On Linux, if a disconnected monitor is
stuck, `./kill_idf_monitor.sh --dry-run` lists this user's monitors;
`./kill_idf_monitor.sh` stops them and restores their terminals. `--force` is a
last resort. The helper affects all matching monitors owned by your user, across
projects. To repair a terminal manually, type `stty sane` and press Ctrl+J.

### Configuration

| Setting | Default | Location |
|---|---:|---|
| Initial rounds on each boot | 20 | `main.c`: initial `state` |
| Rounds after reaching zero | 500 | `main.c`: `update_demo_state()` |
| Demo update delay | 250 ms | `main.c`: main loop |
| Critical threshold (inclusive) | 10 | `app/sentry_ui.h` |
| Duration of each warning phase | 171 ms | `app/sentry_ui.h` |
| Audio enabled | Yes | menuconfig → Aliens sentry |
| Audio volume | 15% | menuconfig → Aliens sentry |
| Start directly in critical test | No | menuconfig → Aliens sentry |

Here `main.c` means `firmware/esp32-2432S028R/main/main.c`.
To change menu settings:

```sh
idf.py -C "$FW" -B "$FW/build" menuconfig
```

Rebuild and flash after changes. The startup-critical test overrides the initial
round count with the configured critical threshold. The firmware is an animated
demo; the desktop keyboard controls are not hardware controls.

### Speaker wiring and synchronization

The standard board feeds its onboard amplifier from **GPIO26 / DAC channel 1**.
Use its **P4 amplified speaker connector** with a suitable passive speaker;
verify the connector and speaker specification for your exact board. Start at
low volume. This is **not a headphone/line output**: do not connect it directly
to headphones or a computer audio input, or ground either speaker terminal.
GPIO25 remains reserved for the touchscreen clock. DAC streaming also uses the
ESP32 I2S0 resource; do not allocate it concurrently to another driver.

Audio combines nine sine partials in two harmonic families around 1324 and
1687 Hz, with a shared 3 ms attack / 4 ms release envelope. At nominal 24 kHz,
each 171 ms phase is exactly 4,104 samples. Compact/dark is audible; expanded/light
is quiet. LVGL follows DMA-completed playback rather than triggering each pulse,
so delayed rendering does not change the audio rhythm. LCD updates still have
refresh/scheduling latency; this is not sample-accurate pixel synchronization.

Buffered audio can delay critical entry, exit, or tap acknowledgement by roughly
up to 120 ms under healthy scheduling, plus the release ramp. Acknowledgement
keeps the playback clock running. Explicit critical reconfiguration re-arms sound.
Ten-second diagnostics report stream progress and rendering gaps. **DMA stops=0**
is expected during healthy streaming. Stream faults select a visual fallback.

## Portable integration

`app/sentry_ui.*` has no SDL or ESP32 dependencies beyond LVGL. Its state API
controls the readouts; `sentry_ui_configure_critical()` sets threshold/timing.
The optional request/playback-clock hooks keep audio and visual phases related
without putting hardware drivers in the shared UI. `app/sentry_bell.*` synthesizes
DAC8 samples and sequences the gates independently of render chunk sizes.

All UI calls must occur on the LVGL thread or under the platform's LVGL lock.
The clock callbacks must not block or re-enter the UI. Register callbacks again
if recreating the UI. The board wiring lives in `main/hardware.h`.

## Tests and CI

CTest covers glyph/label fit, warning lifecycle and threshold boundaries,
playback-driven visual state, sample-accurate sequencing, mute/re-arm behavior,
and identical audio with/without a deliberate 500 ms UI stall. Tests do not prove
physical DAC continuity or touch calibration; test those on the actual board.

GitHub Actions builds/tests the desktop target and builds the ESP32 firmware.
It does not flash hardware. First-run CI fetches dependencies from their upstream
sources. Build outputs and local SDK configuration are intentionally ignored.

## License

Original project code is offered under the [MIT License](LICENSE). Dependencies
retain their own licenses. MIT covers the author's contributions only; it does
not grant rights to third-party film artwork, lettering, trademarks, or other
underlying material. Read [NOTICE.md](NOTICE.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistribution,
especially commercial distribution. No legal clearance is implied.
