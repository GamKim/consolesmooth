# How ConsoleSmooth is measured

[한국어](MEASUREMENT.ko.md) · [Back to README](../README.md)

This page explains how the numbers in the ConsoleSmooth docs and results pages were measured. They are all **the developer's own measurements on one PC**. The goal is that anyone can repeat them with the same method. When you quote a number, please include its date and version.

## Official download

- Installers are published only on **[github.com/GamKim/consolesmooth/releases](https://github.com/GamKim/consolesmooth/releases)**. Files on other sites or re-uploads are not official.
- The installer is not code-signed. After downloading, compare it with `SHA256SUMS-<version>.txt` from the same release:

  ```powershell
  Get-FileHash .\ConsoleSmooth-Plugin-Setup-<version>.exe
  ```

## Test PC

| Item | Details |
|---|---|
| GPU | NVIDIA RTX 5080 (other GPU classes are emulated by locking clocks, so they are approximate) |
| Capture card | AVerMedia GC573 (Live Gamer 4K) |
| Software | AVerMedia Streaming Center 1.9.19.1 |
| Consoles | Switch 2 (60 fps games), PS5 (30 fps games such as GTA VI) |

## 1. Latency: 240 fps phone video

1. Put the **passthrough monitor** (on the capture card's passthrough output, the picture without the PC) next to the **PC monitor**.
2. Film both screens in one shot with a phone at 240 fps slow motion.
3. Use a moment with a clear bright motion (Switch 2 Zelda sword swings; GTA VI opening and closing the pause menu).
4. Extract the brightness signal of each screen from the video and align them (cross-correlation) to find how many ms later the PC screen changes. Each swing is measured separately and averaged.
5. The result is **how many ms after the passthrough the picture appears**. It includes the capture card, Streaming Center, generation and the monitor.

The same method is used for the Streaming Center default preview (generation off) and for other programs' real output. Other programs are used as delivered, with their recommended settings, and ConsoleSmooth is never lowered to match them.

## 2. Latency shown in the app

`original→screen X ms` on the bottom status line of the settings window is the time from an original frame reaching the plugin until its Present call returns, averaged every 2 seconds.

- It does **not** include the capture card, the time before Streaming Center hands the frame to the plugin, or the monitor. It is always smaller than method 1.
- It appears only with the low latency view in full screen (fast path).
- It is for comparing settings on the same PC, not for comparing with other programs' end-to-end latency.

## 3. Quality: PSNR on the same input

1. Record 60 fps gameplay **losslessly** (utvideo straight from the capture device, or OBS "Lossless").
2. Feed only the even frames as input and use the odd frames (the real pictures) as the answer.
3. Compute PSNR between each generated frame and the answer: whole picture, moving areas, HUD area and the worst 5%.
4. Other programs get the same file, and their **actual output** is scored.

## 4. GPU load

- Log `nvidia-smi` every 100 ms and average utilisation (%) and power (W).
- Measure Streaming Center alone first, then look at the increase with generation on.

## 5. Generated and dropped frames

- The log line written every 10 seconds (`GPU_VIEW shown_orig / shown_gen / dropped`) counts how many originals and generated frames really reached the screen.
- A 4K screen recording (ddagrab) is checked for frames shown twice.

## Limits

- One PC, one GPU model and a few scenes.
- Other GPU classes are emulated by lowering the RTX 5080's clocks, so memory speed differs and results may be better than on a real card.
- Other programs were measured at the version available at the time. Results may change after their updates.
