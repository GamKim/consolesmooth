# ConsoleSmooth in 5 minutes (GTA VI · Switch 2)

[한국어](QUICK-START.ko.md) · [简体中文](QUICK-START.zh.md) · [日本語](QUICK-START.ja.md) · [Full guide](GUIDE.md)

If you already use an AVerMedia capture card with **AVerMedia Streaming Center**, you don't need another program. Install one plugin and watch PS5 GTA VI at 60 fps instead of 30, or Switch 2 at 120 fps instead of 60.

## Check first

| Item | Requirement |
|---|---|
| Capture card | AVerMedia. Tested: GC573, GC313 Pro, GC553 Pro. GC553G2 is not tested yet (it uses the same Streaming Center, so it will likely work). |
| Software | AVerMedia Streaming Center 1.8.22.2 or 1.9.19.1. It does not work in OBS or RECentral. |
| GPU | At least AMD RX 6600 · GeForce RTX 2060 class (FSR 3) / RTX 3070 or 4060 class (RIFE). NVIDIA Video Frame Generation needs RTX 40/50. |
| Monitor | 120 Hz or higher |

## Install and turn it on

1. Download `ConsoleSmooth-Plugin-Setup-<version>.exe` from [Releases](https://github.com/GamKim/consolesmooth/releases/latest). The file is not signed: if Windows warns you, click **More info → Run anyway**.
2. **Close Streaming Center completely** (tray icon too), then click **Install**.
3. Start Streaming Center. With the game picture coming in, click **Frame generation settings** on the toolbar → **Recommended** → **Apply**. It tests on your PC for about 25 seconds and picks the settings.
4. Turn it on with the **Turn frame generation on** button or **Ctrl+Alt+F**. Press it again to turn it off.

## What to use

| Game | Multiplier | Note |
|---|---|---|
| PS5 GTA VI (30 fps) | **2x** → 60 | 3x and 4x (→ 90, 120) work too, but add latency and artifacts. Start with 2x. |
| Switch 2 (60 fps) | **2x** → 120 | Fits a 120 Hz monitor. |
| Fighting, rhythm, shooters, competitive games | Don't use it | The picture shows up later than the original, which hurts timing-critical games. |

- For the lowest latency, choose **Full screen** under **Frame generation** tab → Display. The low latency view turns on with it, and full screen stays open when you click other windows. The developer measured about 9-19 ms after the original (RTX 5080).
- To put generated frames into recordings and streams, set Recording source to **With generated** in the **Display · recording** tab.
- The bottom status line of the settings window shows the generated fps and how long an original takes from arrival to screen (measured inside the program).

Problems? See [Troubleshooting in the full guide](GUIDE.md#troubleshooting).
