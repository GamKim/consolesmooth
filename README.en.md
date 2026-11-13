# ConsoleSmooth

[한국어](README.md)

An **unofficial, free frame generation plugin for AVerMedia Streaming Center**. It inserts in-between frames into the console picture coming from your capture card (Switch 2, PS5, usually 60 fps), so the Streaming Center preview runs at 120 or 160 fps. Generated frames can also be recorded.

A personal project, not affiliated with or endorsed by AVerMedia. No AVerMedia software or files are included.

## Features

- **Runs inside Streaming Center**: toggle it from the toolbar, no window capture or overlay.
- **Records generated frames**: set the recording source to "With generated frames" for files at the output rate (e.g. 120/160 fps). For a 30 fps game the file is still 120 fps, but the new pictures are game fps x multiplier (60 per second at 2x).
- **Two models**: RIFE 4.6 (default, quality) and AMD FSR 3 (2x and 4x, light).
- **HUD protection**: fixed HUD (health bars, maps, button labels, including semi-transparent ones) is kept from the original.
- **30 fps smoothing**: 30 fps games become "game fps × multiplier", evenly spaced (2x: 30 -> 60, every 16.7 ms), about +8 ms latency.
- **FPS readout**: by default `input / now fps` (capture signal / frames shown per second, e.g. 60 / 120). With "Show the game's real frame rate" on: `original / now` (pictures the game changed / new pictures shown, e.g. a 30 fps game at 2x reads 30 / 60).
- **Low latency view (beta)**: shows the incoming picture in full screen right away (also with generation off). While it is on, recording and streaming get no picture. PS5 menu, 240 fps phone, 11-13: about 23 ms behind passthrough (the Streaming Center preview with generation off: about 31-33 ms).
- **59.94 Hz input (PS5)**, **recommended settings** (a real 25 s trial), Korean/English UI.

## Requirements

- An AVerMedia capture card + Streaming Center 1.8.22.2 or the latest 1.9.19.1, x64 (tested: Live Gamer 4K GC573, GC313 Pro, GC553 Pro)
- Windows x64 and a Vulkan GPU. Minimum: **AMD Radeon RX 6600** (1080p 60 → 120 fps); on GeForce about RTX 2060 class (approximate test).
- A monitor of 120 Hz or more
- For 120 fps recordings raise the Streaming Center recording bitrate to 40-50 Mbps (15 Mbps smears detail).

## Install

Download `ConsoleSmooth-Plugin-Setup-1.0.1.exe` from [Releases](https://github.com/GamKim/consolesmooth/releases) and run it. The installer is not signed; if SmartScreen warns, choose "More info" → "Run anyway". Usage: [installer/INSTALL-README-EN.txt](installer/INSTALL-README-EN.txt).

## Uninstall

Close Streaming Center, run `Uninstall.exe` in the `Plugin\ConsoleSmooth` folder and press **Uninstall** (default location: `C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center\Plugin\ConsoleSmooth`). Streaming Center itself and your settings are kept. Details: [installer/UNINSTALL-README-EN.txt](installer/UNINSTALL-README-EN.txt).

## Measurements (RTX 5080, GC573, 1080p)

Comparison videos (quality, latency, Lossless Scaling) and settings window photos are in the [media](media/README.md) folder.

| Input | Setting | Generated |
|---|---|---|
| Switch 2 (60 fps) | RIFE 2x → 120 fps | 99%+ |
| Switch 2 (60 fps) | RIFE target 160 fps | 98% |
| PS5 GTA V (59.94 Hz) | RIFE 2x / RIFE 160 / FSR 3 120 | 96.4% / 91.6% / 100% |
| Switch 2 (RX 6600) | RIFE 2x (1/4-size motion) / FSR 3 | 99.8% / 98% |
| PS5 GTA V quality mode (30 fps) | RIFE 2x + 30 fps smoothing | new pictures 30 -> 58-60, 16.7 ms apart, buffer 33.4 ms |

New pictures per second counted from the recordings (2026-11-11, repeated runs):

| Game | Off | 2x |
|---|---|---|
| Switch, Zelda: Ocarina of Time (60 fps, 5 scenes) | 57-60 | 114-119 |
| PS5 GTA V performance (60 fps, 2 runs) | 52-60 | 103-111 |
| PS5 GTA V quality (30 fps, 8 runs) | 30-31 | 58-62 |
| PS5 Bloodborne (30 fps, 30 fps smoothing on) | 30 | 60 (evenly every 16.7 ms, 98%) |

Delay (capture excluded, generation buffer + display): window about 37-39 ms, full screen about 23-24 ms. Filmed at 240 fps next to passthrough, the plugin added about +36-50 ms (window) and +11-33 ms (full screen) over the plain preview (PS5, Switch). The preview is later than passthrough, so competitive and rhythm games are not recommended.


30 fps games, picture quality (1.0.1, compared with real frames, higher is better): GTA V driving 24.60 -> 25.11 dB, GTA V walking 23.48 -> 24.01 dB, Zelda (a 60 fps game tested with a 33 ms gap) 23.29 -> 23.72 dB. For 30 fps games each generated frame is made both ways (a->b and b->a) and averaged when there is time, which shrinks the transparent halo around a turning character. Thin objects that suddenly appear, such as a pole, can still break up.

## Limits

- Very fast camera turns can make the background shimmer slightly. In 30 fps games a face can wobble on a sudden head turn.
- In third-person games a character standing still can be taken for HUD by HUD protection (broken hair edge: turn HUD protection off).
- If letters break when a see-through text box (help, menus) or an in-game web page moves, turn HUD protection off.
- No HDR/10-bit or 8K. Input is 30-120 fps (29.97/59.94/119.88 included).
- 4K (3840x2160) processing: in engine measurements on an RTX 5080, RIFE 2x fits (motion estimated at quarter size automatically, about 14 ms per frame); 3x and more fall short. 1080p is still recommended for latency and headroom.
- A new Streaming Center version may need a new check.

## Layout

| Folder | Contents |
|---|---|
| `engine/` | Frame generation engine DLL (C++17): real-time scheduler, RIFE/FSR 3 glue, HUD protection, checks (`checks/`) |
| `plugin/` | Streaming Center plugin (C, GStreamer tracer): preview hook, settings window, input detection, full-screen display |
| `installer/` | Installer (C#, .NET Framework 4) and installer test |
| `third_party/` | rife-ncnn-vulkan (with ncnn, glslang), part of FidelityFX SDK 1.1.4, Vulkan-Headers. Our changes: `third_party/PATCHES/` |
| `tools/` | Toolchain download (`Get-Toolchain.ps1`), one-shot build (`Build-All.ps1`) |


## Problem reports

Attach the file from **Export diagnostics** (settings window) to an [issue](https://github.com/GamKim/consolesmooth/issues).

## License

MIT ([LICENSE](LICENSE)). Third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
