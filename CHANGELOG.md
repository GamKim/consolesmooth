# Changelog

## 1.1.5 — works when the graphics driver left no vulkan-1.dll (2026-11-23)

- The frame generation engine needs `vulkan-1.dll`, the Vulkan loader that normally comes with the graphics driver. On a PC without it the engine could not be opened (`FG_ENGINE_LOAD_FAILED win32=126`) and nothing was generated. The installer now includes the official loader (LunarG Vulkan Runtime 1.4.363.0, signed, unmodified) in the plugin folder. The driver's own file is always used first; the included one only when Windows has none. Nothing is written to the Windows folder, and uninstalling removes it.
- The loader is not a driver: if the Vulkan part of the graphics driver itself is missing, the graphics driver still has to be reinstalled.
- When the engine file cannot be opened, the settings window, the failed Recommended settings page and the diagnostics say why. In 1.1.4 they said "engine did not start in 10 s", and the retry without overlays added in 1.1.4 could not help in this case.
- Tested on an RTX 5080 PC by making the engine unable to find the system file. Not yet tested on the PC that had the problem (RTX 3070).

## 1.1.4 — full screen is the low latency view, latency in the settings window (2026-11-21)

- **Full screen turns the low latency view on.** Choosing Full screen under Frame generation tab → Display now also turns the low latency view on (and Window turns it off), so playing in full screen always uses the fastest path. Before, the two were separate switches and full screen alone used the slower path.
- **Full screen stays open when you click other programs.** A window on another monitor (Discord, a browser) leaves full screen on top. A window on the same monitor (or Alt+Tab) moves full screen behind it while it keeps running; clicking full screen or Alt+Tab back brings it on top again. Before, any window on the same monitor closed full screen.
- **Latency in the settings window.** On the fast path the bottom status line shows `On · fast path · original→screen X ms`, the time from an original frame arriving to it being handed to the screen, averaged every 2 s. Before, the fast path was shown as "Engine starting".
- **The reason for a failed start is in the diagnostics.** When the frame generation engine cannot start (the graphics card cannot be opened, a model file is missing, …), the reason is written to the log (`FG_ENGINE_ERROR`), to the exported diagnostics (`gpu_detail engine_error=…`) and to the failed Recommended settings page (report: RX 9070 XT, both RIFE and FSR 3 failed with no reason shown).
- **Retry without Vulkan overlay layers.** If the graphics card cannot be opened on the first try, the engine tries once more with the Vulkan overlay layers of other programs (RivaTuner, Overwolf, Steam, Discord, ReShade …) turned off for ConsoleSmooth only. A PC that starts normally never reaches the retry. The diagnostics show when the retry was used (`implicit_layers=off`).
- **The diagnostics now list the three usual suspects:** Windows build and Insider channel (`windows_build=… insider=…`), graphics driver versions (`gpu_driver …`) and the installed Vulkan overlay layers (`vulkan_implicit_layer …`).
- New guides: 5-minute quick start in Korean, English, Chinese and Japanese (`docs/QUICK-START.*.md`) and how latency and quality are measured (`docs/MEASUREMENT.md`).
- Latency unchanged (RTX 5080, PS5 GTA VI, NVIDIA Video Frame Generation 2x, full screen): the presentation mode stays Hardware: Independent Flip while another monitor is clicked and after Alt+Tab, and a 240 fps phone recording of passthrough and PC screen showed the same delay as 1.1.3 within one camera frame (4 ms).

## 1.1.3 — 3x and 4x with NVIDIA Video Frame Generation and RIFE (2026-11-20)

- NVIDIA Video Frame Generation always uses the fast path, which made only the midpoint: choosing 3x or 4x still gave 2x (the settings window showed the higher rate). The fast path now makes 2 (3x) or 3 (4x) pictures per source at 1/3, 2/3 or 1/4, 1/2, 3/4 for NVIDIA Video Frame Generation, RIFE 4.6 and RIFE 4.15 lite, each with HUD protection, shown in order in the low latency view and in the Streaming Center output (recording, streaming). RIFE 3x/4x used the standard path before and now stays on the fast path.
- FSR 3, DLSS Frame Generation and AMD FSR Frame Generation make only the midpoint on the fast path; at 3x/4x they use the standard path as before.
- Measured (RTX 5080, PS5 GTA VI, 30 fps game, 10 s): NVIDIA Video Frame Generation 4x 299 originals + 897 generated (120 new pictures per second), 0 dropped, original on screen 16.0 ms after arrival (2x: 11.8 ms); with RTX Video HDR the same. RIFE 4.6 4x: 297 + 870, about 3% of generated pictures dropped as late; RIFE 4.15 lite 4x: 297 + 859, about 4%. 2x unchanged (NVIDIA Video Frame Generation, RIFE 4.6, DLSS Frame Generation: no drops).
- With "Show generated frames briefly" on, each generated picture stays half an even step (4x on a 30 fps game: about 4 ms each, original about 21 ms); off = even spacing.

## 1.1.2 — generated frames shown briefly, RTX Video HDR (2026-11-19)

- Fast path (low latency view + full screen + 2x): the generated frame now stays on screen about a quarter of a source interval and the original about three quarters (30 fps game: 8.3 ms and 25 ms; before 16.7 ms each). Generated frames are softer than originals (RTX 5080, PS5 GTA VI walk: about 50-70% of the original's sharpness in every model), and showing them half the time made heads and edges look smeared and the picture flicker. Measured in desktop captures with all six models: generated 8.2-8.8 ms, original 24.6-25.5 ms, 59.9 new pictures per second (nothing dropped). New switch in the Display tab, **Show generated frames briefly** (on by default); turn it off for evenly spaced frames as in 1.1.1.
- RTX Video HDR in the full screen window (Advanced tab, off by default): the NVIDIA driver's RTX Video HDR (the same driver feature video players use) turns the SDR picture into HDR10; nothing to download. NVIDIA RTX GPU and an HDR monitor needed; if Windows HDR is off it is turned on while the full screen is open and off again when it closes. Before use a white test picture is checked; if the driver does not convert it, the window stays SDR. The FPS readout is shown in the HDR window too. Recording and streaming stay SDR. RTX 5080, NVIDIA Video Frame Generation: SDR white → about 650 nits, all frames shown (297 originals + 298 generated per 10 s), arrival → original on screen 11.9 ms (same as without HDR).
- Settings window: taller so no text is cut off (the short-frame and RTX Video HDR rows sit in their own cards); drag the title bar to move it and the bottom-right corner or edges to resize it (60-200%); position and size are remembered. The title shows the right version (1.1.1 still said 1.1.0).
- 30 fps smoothing no longer sends RIFE, FSR 3, AMD FSR Frame Generation and DLSS Frame Generation to the standard path; they stay on the fast path. GTA VI, 1.1.1 → 1.1.2: RIFE 4.6 GPU 52% / 106 W → 15-17% / 70 W, DLSS Frame Generation 44% / 93 W → 11-13% / 60 W.

## 1.1.1 — recording and streaming fixes (2026-11-18)

- Recording and streaming go on while the low latency view is shown (1.1.0 stopped them; turning the view on during a recording froze the file). Generated frames sent to recording use the HUD-protected copy.
- "With generated" recording source: recording and streaming run at the output rate (e.g. 120 fps) from the start, so generation can be turned on or off during a recording or stream (pictures repeat while it is off). Before, the choice was fixed when the recording started.
- Fast path (low latency view + full screen + 2x, and NVIDIA Video Frame Generation always): recordings came out empty (0 bytes) because the recording route found no original frames; fixed.
- Fast path with 30 fps games: recordings and the Streaming Center preview had no generated frames (each 30 fps source was taken for a still picture at 120 ticks); the generated frame is now held for half a source interval. Measured in recordings (RTX 5080): Switch 2 Zelda 60 fps → 120 new pictures per second, PS5 GTA V 30 fps → 60; a local RTMP stream test (GTA V) also 30 → 60.
- Fast path: after the low latency window closed (for example clicking another window on the same monitor), the Streaming Center preview flickered between a normal and a darker picture; generated frames now have full alpha.
- Note: Streaming Center streams at the preview rate (e.g. 120 fps) even when the stream setting shows 60.

## 1.1.0 — low latency view, six models (2026-11-18)

- Low latency view is a regular feature (no longer beta). With low latency view + full screen + Low latency + 2x, the host's GPU frames are shared with the model (no CPU copies), the made frame is shown as soon as it is ready and the original half a source interval later, straight into the full screen window (independent flip). 240 fps phone, Switch 2 Zelda, RTX 5080, delay after the passthrough: FSR 3 8.8 ms, RIFE 4.6 11.8, AMD FSR Frame Generation 12.5, NVIDIA Video Frame Generation 12.6, RIFE 4.15 lite 12.7, NVIDIA DLSS Frame Generation 19.0 (1.0.1: RIFE 4.6 83.0, FSR 3 76.7; Streaming Center preview without generation 31.9).
- The fast path is chosen when Streaming Center starts; the settings window says when a restart changes it. Other settings (3x/4x, target fps, 30 fps smoothing, stable, two-GPU laptops) use the standard path. A fast-path failure is remembered per model and the next start uses the standard path; choosing the model again retries.
- Uneven source hand-over by the host (8/25 ms gaps) no longer drops shown frames on the fast path.
- Models: RIFE 4.6, RIFE 4.15 lite, FSR 3, AMD FSR Frame Generation (AMD FidelityFX SDK 2.4 DLLs, 2x), NVIDIA Video Frame Generation (RTX 40/50, runtime downloaded from NVIDIA on first use), NVIDIA DLSS Frame Generation (RTX 40/50, runtime nvngx_dlssg.dll 310.9.16 bundled in the installer under the NVIDIA RTX SDKs license).
- Recommended settings: generation buffer = half an output tick + 3 x measured time + 4 ms, 15-25 ms (buffer sweep, RTX 5080: 99%+ generated needed AMD 17, DLSS 20, RIFE/FSR 3 25 ms).

## 1.0.1 — first public release (Streaming Center 1.9.19.1 and 1.8.22.2)

1.0.1 is the first version published. 1.0.0 was built and tested but never released; its contents are listed below and are all part of 1.0.1.

- 11-14 rebuild (internal 2.2.4 folded into 1.0.1): on/off shortcut (default Ctrl+Alt+F, recordable in the Advanced tab); the settings window only saves and choices take effect at the next turn-on with the toolbar button or shortcut (full screen and low latency view open then; the settings on/off switch is gone); display sync V-Sync / variable refresh / off for window and full screen (full screen presents with tearing allowed when DXGI supports it); recommended settings names the GPU, reads the monitor EDID for variable refresh, keeps the low-latency buffer at 17 ms or more and picks RIFE only when its live time per frame is at most 0.85 x the source gap; only the pipeline holding the capture source generates (scene preview pipelines pass through; before, a moving game let a scene thumbnail take the GPU: main picture 16.8% vs thumbnail 77.7% on an RTX 5080 locked at 2100 MHz); the FPS readout no longer says "generation short"; Esc in full screen (and the low latency view) also turns frame generation off, so one press of the button or shortcut opens it again (closing by clicking another window keeps generation on).

- Input 30-120 fps (29.97/59.94/119.88 included).
- FSR 3 at 4x: FSR 3 only makes the midpoint, so 4x halves twice (a second FSR 3 stream gets the original, the midpoint and the next original and makes the 1/4 and 3/4 pictures). 3x stays unavailable for FSR 3. Replay of real Switch (Zelda, 60 fps) frames at 240 (RTX 5080, 11-14, 3 runs): 92% of in-between pictures generated with HUD protection on, 100% with it off. 120 fps input at 2x: 88-90% (on), 96-98% (off).
- Low latency view (beta, settings > Display): the full-screen window shows the incoming picture right away, also with generation off; recording and streaming get no picture while it is on. PS5 menu, 240 fps phone (11-13): about 23 ms behind passthrough, Streaming Center preview with generation off about 31-33 ms; LLCV (a separate low-latency capture viewer) in exclusive full screen: 13 ms.
- FPS readout: by default input / shown (e.g. 60 / 120); the switch "Show the game's real frame rate" shows the game / shown rates as before (e.g. 30 / 60).
- HUD protection: when map content moves between the two originals (an in-game web page starting to scroll), those 32 px blocks are left out of the map for that pair, so old letters are no longer pasted over the moved page; HUD that changes in place stays protected (Zelda HUD replay: 19 of 418 pairs affected). Dense static text near a see-through box counts after 30 sources instead of 150. The settings note says to turn HUD protection off if text boxes or web pages still break.
- Recommended settings no longer turns 30fps smoothing on for a 30 fps game (it only says so).
- Works with Streaming Center 1.9.19.1 (GStreamer 1.28, Qt 6). That version no longer enumerates GStreamer device providers, so 1.0.0 never started there. The installer now adds "consolesmooth" to the user's GST_TRACERS environment variable, and the plugin activates when GStreamer creates its tracer; older Streaming Center versions still start it as before. Uninstalling removes only this entry.
- The toolbar buttons follow the Streaming Center version (read from ApInfo.ini): 1.9.19 added pen and pointer buttons, so the two buttons now sit after them instead of on top of them.
- The plugin waits longer for the Streaming Center window (an update dialog can come first).
- Engine 1.0.1, 30 fps games: each generated frame is made twice (a->b and b->a) and averaged when the deadline allows (30 fps content only, RIFE only), with 30 fps smoothing on or off. Ground truth on real frames: Zelda 60 fps (33 ms gap) 23.29 -> 23.72 dB, GTA5 30 fps 23.48 -> 24.01 dB, worst 10% 22.10 -> 22.59 dB; the transparent halo around a turning character becomes much smaller. Live PS5 GTA5: applied to every generated frame, about 11 ms instead of 6.5 ms per frame on an RTX 5080, misses about 0.2% with smoothing on (margin 3 single passes + 2 ms). CS_LIVE_TTA=0 turns it off.
- Engine 1.0.1, stable mode: a 30 fps game switches RIFE flow from full to half size at FHD (ground truth GTA5 23.52 vs 21.78 dB); with full size the reversed pass fitted 21 of \~1340 frames, with half all of them. CS_LIVE_STABLE30=full keeps full size.
- Engine 1.0.1: output slots keep the next slot unless the display clock is clearly elsewhere (0.6 slot), so a display clock near a half-slot boundary no longer alternates 1/3-slot steps with 30 fps smoothing (seen 0.2-3.9 times/s depending on the start-up phase). CS_LIVE_SLOT=round restores the old rounding.
- The GC573 SDK input check (exact input rate, 59.94 Hz handling) also binds to the 1.9.19.1 avtsdk.dll: its bound functions are the same code as in 1.8.22.2 (only address displacements differ); the state canaries stay in place.
- Same plugin, same Switch input, RIFE 2x, low latency 15 ms, window display, 1.8.22.2 vs 1.9.19.1 test copies (11-12): generation 99.4% / 98.8%, generation buffer 17.7 / 18.3 ms, Streaming Center compositing 5.1 / 4.2 ms, present to screen about 15 ms on both (Windows composed flip), engine input identical (BGRA 1920x1080 sRGB). Ground truth on 250 real frame pairs: 22.7 / 23.1 dB (repeat baseline 16.8 / 16.8 dB). No difference beyond run-to-run variation.

## 1.0.0 — not released (included in 1.0.1)

Built and tested 11-11 but not published. Earlier builds (0.x to 2.1.8) were internal test builds; version numbers in source comments such as "1.9.5" or "2.0.1" refer to those internal builds.

Included in 1.0.0:
- RIFE 4.6 and FSR 3 (2x) frame generation inside the Streaming Center preview, 2-4x or a target fps, low-latency and stable buffers.
- Recording with generated frames.
- HUD protection, including semi-transparent HUD elements; outside the detected HUD an original pixel is put back only where the generated pixel already agrees with it (no dots on thin moving objects with 30 fps smoothing).
- 30 fps smoothing: the multiplier applies to the game's real frame rate (a 30 fps game at 2x -> 60, 3x -> 90, 4x -> 120, evenly spaced), about +8 ms buffer; 59.94 Hz (NTSC) input with targets scaled by 1000/1001.
- Low-latency buffer that rises to the measured processing time at start and steps back down once stable.
- Latency priority switch (quarter-size RIFE motion), full-screen display with optional sharpening.
- Recommended settings (a real 25 s trial of both models), GPU clock keep (default on for AMD), FSR 3 fast path.
- Idle preview groups (scene thumbnails whose picture never changes) are passed through without an engine.
- FPS readout of the real rates, numbers only: original (changed game pictures per second) / now (changed originals plus generated frames shown per second), for example "30 / 120 fps"; a still picture reads 0 / 0.
- Diagnostics export (with a note on what it is for), Korean/English UI following the Streaming Center language; the settings window always writes the full name "Streaming Center".
- Installer with verify, repair, tamper detection and a dedicated uninstaller.
