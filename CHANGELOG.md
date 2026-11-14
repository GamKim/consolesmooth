# Changelog

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
