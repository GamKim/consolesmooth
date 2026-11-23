# ConsoleSmooth

[한국어](README.ko.md)

An **unofficial, free frame generation plugin** for AVerMedia Streaming Center. It adds in-between frames to a console picture coming in through a capture card (Switch 2, PS5 and so on, usually 60 fps) and shows it at 120 or 160 fps. Recordings can include the generated frames.

This is a personal project, not affiliated with AVerMedia, and it contains no AVerMedia programs or files.

**New here? Follow the [guide](docs/GUIDE.md)** (install, first run, settings, troubleshooting).

**Test results at a glance: https://gamkim.github.io/consolesmooth/** (latency and quality tables with the comparison videos, in Korean)

## What is new in 1.1.5

- **Works on PCs without `vulkan-1.dll`.** The graphics driver normally installs this file; without it the engine could not be opened and nothing was generated (`FG_ENGINE_LOAD_FAILED win32=126` in the diagnostics). The installer now includes the official file (LunarG) and uses it only when Windows has none. The Windows folder is not changed. If the Vulkan part of the graphics driver itself is missing, reinstall the driver.
- When the engine file cannot be opened, the settings window, the failed Recommended settings page and the diagnostics say why.

## What is new in 1.1.4

- **Full screen turns the low latency view on.** Pick Full screen when you play and you get the fastest path (Window turns the low latency view off).
- **Full screen stays open when you click other programs.** It stays on top while you use a window on another monitor; a window on the same monitor or Alt+Tab moves it behind for a while, and clicking it brings it back.
- **Latency on the bottom status line of the settings window:** `On · fast path · original→screen X ms` (measured inside the program).
- When the frame generation engine cannot start, the reason shows in the diagnostics and on the failed Recommended settings page. If something goes wrong, attach the file from Advanced tab → export diagnostics to an [issue](https://github.com/GamKim/consolesmooth/issues).
- If the graphics card cannot be opened on the first try, it tries once more with other programs' Vulkan overlays (RivaTuner, Overwolf, Steam, Discord, ReShade …) turned off. The diagnostics now include the Windows build and Insider channel, graphics driver versions and the installed Vulkan overlays.
- 5-minute quick start in Korean, English, Chinese and Japanese: [docs/QUICK-START.en.md](docs/QUICK-START.en.md) · How we measure: [docs/MEASUREMENT.md](docs/MEASUREMENT.md)
- Latency is the same as 1.1.3 (240 fps recording next to the passthrough, within 4 ms).

## What is new in 1.1.3

- **3x/4x with NVIDIA Video Frame Generation and RIFE**: NVIDIA Video Frame Generation always uses the fast path, which made only one picture between originals, so 3x/4x gave 2x. The fast path now makes 2 (3x) or 3 (4x) pictures for NVIDIA Video Frame Generation, RIFE 4.6 and RIFE 4.15 lite, in the low latency view and in recording/streaming. 30 fps game at 4x = 120 new pictures per second (RTX 5080, no drops).
- FSR 3, DLSS Frame Generation and AMD FSR Frame Generation stay 2x on the fast path; 3x/4x use the standard path as before.

## What is new in 1.1.2

- **Generated frames are shown briefly**: generated frames are softer than originals (about 50-70% of the sharpness in every model), so showing them half the time made heads and edges smear and the picture flicker. With low latency view + full screen, a generated frame is now shown for about 8 ms and the original for about 25 ms (30 fps games). Display tab → **Show generated frames briefly** (on by default); turn it off for even spacing as in 1.1.1.
- **RTX Video HDR**: Advanced tab → **RTX Video HDR** shows the full screen in HDR with the NVIDIA driver's RTX Video HDR (nothing to download). Needs an NVIDIA RTX GPU and an HDR monitor; Windows HDR is turned on only while the full screen is open if it was off. Recording and streaming are unchanged.
- **30 fps smoothing keeps the fast path**: RIFE, FSR 3, AMD FSR Frame Generation and DLSS Frame Generation no longer fall back to the standard path, so GPU use drops a lot (GTA VI, RIFE 4.6: 52% → 15-17%).

## What is new in 1.1.1

- **Recording and streaming**: they go on while the low latency view is shown, and with the recording source "With generated" they run at the output rate from the start, so generation can be turned on later. Fixed empty recordings and missing generated frames in 30 fps games on the fast path. Measured: 60 fps game 60 → 120 new pictures per second, 30 fps game 30 → 60 (recording and a local stream test).

## What is new in 1.1.0

- **Low latency view is a regular feature**: with frame generation on, the picture appears only **about 9 to 19 ms** after the original (passthrough). 1.0.1 took 77 to 87 ms (RIFE, FSR 3). It is even earlier than the default Streaming Center preview without generation (about 32 ms).
- **Six models**: RIFE 4.6, RIFE 4.15 lite, FSR 3, AMD FSR Frame Generation, NVIDIA Video Frame Generation, NVIDIA DLSS Frame Generation.
- **Recommended settings** measure the graphics card and pick the generation buffer between 15 and 25 ms.

## Features

- **Works inside Streaming Center**: no window capture or overlay. Turn it on and off with the toolbar button or a shortcut (Ctrl+Alt+F by default, changeable in the Advanced tab). In full screen, Esc closes it and turns generation off. The settings window only chooses and saves; settings apply when you turn it on.
- **Low latency view**: the on button or shortcut shows the picture in full screen right away. With low latency view + full screen + 2x, frames are made on the graphics card and sent straight to the screen (the fast path). Recording and streaming go on while it is shown.
  - The fast path is chosen when Streaming Center starts. If you change the model or display settings, the settings window tells you to restart.
  - If the fast path does not work on a PC, the next start uses the standard path automatically.
- **Record and stream generated frames**: with the recording source set to "With generated", recording and streaming run at the output rate (e.g. 120 fps) from the start. Turning generation or the low latency view on or off during a recording or stream does not interrupt it (while generation is off, each picture repeats). New pictures per second = game fps × multiplier (60 fps game at 2x → 120, 30 fps game at 2x → 60).
- **On-screen display protection (HUD keep)**: fixed screen elements such as health bars, maps and button prompts (semi-transparent ones too) are taken from the original to reduce wobble.
- **30 fps smoothing**: shows 30 fps games evenly at game fps × multiplier (2x → 30 → 60, every 16.7 ms). About +8 ms latency.
- **On-screen numbers**: `input / now fps` by default (capture signal / pictures shown per second, e.g. 60 / 120).
- **Display sync**: V-Sync (default) / variable refresh (G-Sync, FreeSync) / off.
- **59.94 Hz input (PS5)**, **recommended settings** (a real test run plus the graphics card and the monitor's variable refresh support), Korean and English.

## Models

| Model | Notes | Graphics card |
|---|---|---|
| RIFE 4.6 (default) | Balance of quality and speed | Vulkan GPU |
| RIFE 4.15 lite | A little better than RIFE 4.6, a little heavier | Vulkan GPU |
| FSR 3 | Light, 2x and 4x | Vulkan 1.3 GPU |
| AMD FSR Frame Generation | AMD's own frame generation DLL, lightest (2x) | DirectX 12 GPU |
| NVIDIA Video Frame Generation | Best quality. The runtime is downloaded from NVIDIA the first time you choose it | NVIDIA RTX 40/50 |
| NVIDIA DLSS Frame Generation | Light (2x, 3x, 4x; 3x/4x on RTX 50) | NVIDIA RTX 40/50 |

Score of the 60 fps made from the same 30 fps input against a real 60 fps recording (higher is better, Switch 2 Zelda night forest, 701 frames): NVIDIA Video Frame Generation 33.22 dB, RIFE 4.15 lite 33.05, RIFE 4.6 32.94, DLSS Frame Generation 32.64, FSR 3 32.55, AMD FSR Frame Generation 31.04.

## Requirements

- AVerMedia capture card + Streaming Center 1.8.22.2 or the latest 1.9.19.1 x64 (tested: Live Gamer 4K GC573, GC313 Pro, GC553 Pro)
- Windows 10/11 x64
- Graphics card: see the model table. Minimum: **AMD Radeon RX 6600** (FHD 60 → 120 fps, checked on the standard path). GeForce: about RTX 2060 for FSR 3, RTX 3070 / 4060 or better for RIFE (approximate).
- A monitor of 120 Hz or more
- For 120 fps recordings raise the Streaming Center recording bitrate to 40\~50 Mbps.

## Install

Download `ConsoleSmooth-Plugin-Setup-1.1.5.exe` from [Releases](https://github.com/GamKim/consolesmooth/releases) and run it. The installer is not signed; if SmartScreen warns, choose "More info → Run anyway". Illustrated guide: [docs/GUIDE.md](docs/GUIDE.md). All settings: [installer/INSTALL-README-EN.txt](installer/INSTALL-README-EN.txt).

## Uninstall

Close Streaming Center, run `Uninstall.exe` in the `Plugin\ConsoleSmooth` folder and press **Uninstall** (default location: `C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center\Plugin\ConsoleSmooth`). Streaming Center itself and its settings stay. Details: [installer/UNINSTALL-README-EN.txt](installer/UNINSTALL-README-EN.txt).

## Measurements (RTX 5080, GC573, FHD, 2026-11-17 and 18)

### Latency (how long after the original passthrough picture)

A 240 fps phone filmed the original monitor and the PC screen together; the time between the sword trail appearing on the two screens was measured (Switch 2 Zelda, average of 3 swings).

| Screen | Generation | Latency |
|---|---|---|
| ConsoleSmooth 1.1.0 low latency view · FSR 3 | on | 8.8 ms |
| ConsoleSmooth 1.1.0 low latency view · RIFE 4.6 | on | 11.8 ms |
| ConsoleSmooth 1.1.0 low latency view · AMD FSR Frame Generation | on | 12.5 ms |
| ConsoleSmooth 1.1.0 low latency view · NVIDIA Video Frame Generation | on | 12.6 ms |
| ConsoleSmooth 1.1.0 low latency view · RIFE 4.15 lite | on | 12.7 ms |
| ConsoleSmooth 1.1.0 low latency view · NVIDIA DLSS Frame Generation | on | 19.0 ms |
| Streaming Center default preview | off | 31.9 ms |
| ConsoleSmooth 1.0.1 low latency view · RIFE 4.6 / FSR 3 | on | 83.0 / 76.7 ms |

It is still later than the passthrough, so for fighting or rhythm games use the passthrough screen.

### Comparison videos

YouTube (4K, click a picture):

**Latency** (240 fps phone, 8x slower; small screen = original passthrough, large screen = PC)

| Generation on: ConsoleSmooth · Veyra · Lossless Scaling | 1.0.1 vs 1.1.0 |
|---|---|
| [![Latency 1](https://img.youtube.com/vi/kwYfBRFG5Do/hqdefault.jpg)](https://youtu.be/kwYfBRFG5Do) | [![Latency 2](https://img.youtube.com/vi/GAScSgJt1fw/hqdefault.jpg)](https://youtu.be/GAScSgJt1fw) |
| [1.1.0, the other four models](https://youtu.be/zf4zC5MGOJ8) | [ConsoleSmooth with generation vs three previews without](https://youtu.be/AzRhoV9cnZs) |

**Quality** (60 fps made from the same 30 fps input, each cell 1920×1080 at original size)

| Real 60 fps vs RIFE 4.6 · RIFE 4.15 lite · NVIDIA Video Frame Generation | ConsoleSmooth vs Veyra · NVIDIA technologies |
|---|---|
| [![Quality 1](https://img.youtube.com/vi/881Qw9zsiYA/hqdefault.jpg)](https://youtu.be/881Qw9zsiYA) | [![Quality 2](https://img.youtube.com/vi/JvTUElCef24/hqdefault.jpg)](https://youtu.be/JvTUElCef24) |
| [Made frames only, half speed](https://youtu.be/dBSYabFpO98) | [Made frames only, half speed](https://youtu.be/TdxeY9D8SIE) |

[ConsoleSmooth vs Veyra · FSR 3 · FSR 3.1 · AMD FSR Frame Generation](https://youtu.be/Ph15SAfiZmE) ([made frames only, half speed](https://youtu.be/0ZJzv03232M))

More videos and settings window pictures are in [media](media/README.md).

### Generation rate

| Input | Settings | Generated |
|---|---|---|
| Switch 2 (60 fps) | Low latency view · all six models 2x → 120 fps | 597 to 600 of 600 per 10 s |
| Switch 2 (60 fps) | RIFE target 160 fps | 98% |
| PS5 GTA5 (59.94 Hz) | RIFE 2x / RIFE 160 / FSR 3 120 | 96.4% / 91.6% / 100% |
| Switch 2 (RX 6600) | RIFE 2x (quarter-size motion) / FSR 3 | 99.8% / 98% |

## Limits

- The fast path of the low latency view is used at 2x only. 3x/4x, target fps, 30 fps smoothing, stable mode and laptops with two graphics cards use the standard path (generation buffer 15 to 25 ms).
- The fast path was tested on an RTX 5080 only. Please report problems on other graphics cards in [Issues](https://github.com/GamKim/consolesmooth/issues).
- Very fast camera turns can make the background ripple a little. In 30 fps games a face can ripple when the head turns suddenly.
- In third-person games a character standing still for a long time can be taken for an on-screen element (hair tips break → turn display protection off).
- HDR, 10-bit and 8K are not supported. Input 30\~120 fps (including 29.97, 59.94, 119.88).
- A new Streaming Center version may need checking again.

## Layout

| Folder | Contents |
|---|---|
| `engine/` | Frame generation engine DLL (C++17): live scheduler, models (RIFE, FSR 3, AMD, NVIDIA), shared-texture GPU path, display protection, checks (`checks/`) |
| `plugin/` | Streaming Center plugin (C, GStreamer tracer): preview hook, settings window, input detection, full screen and low latency view |
| `installer/` | Installer (C#, .NET Framework 4) and installer test |
| `third_party/` | rife-ncnn-vulkan (with ncnn and glslang), parts of FidelityFX SDK 1.1.4, Vulkan-Headers. Changes in `third_party/PATCHES/` |
| `tools/` | Get the build tools (`Get-Toolchain.ps1`), build everything (`Build-All.ps1`) |

AMD FSR Frame Generation and NVIDIA DLSS Frame Generation use small bridge DLLs (`cs_amdfg.dll`, `cs_dlssg.dll`) built with each company's SDK and shipped in the installer. The NVIDIA DLSS runtime (`nvngx_dlssg.dll`) and AMD's signed DLLs are only in the installer, not in this repository.

## Reporting problems

Attach the file from **Export diagnostics** in the settings window to an [Issue](https://github.com/GamKim/consolesmooth/issues).

## License

MIT ([LICENSE](LICENSE)). Licenses of bundled parts: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This software uses NVIDIA DLSS. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation.
