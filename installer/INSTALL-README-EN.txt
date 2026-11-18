ConsoleSmooth 1.1.0 — Frame generation for AVerMedia Streaming Center

Unofficial plugin, not affiliated with AVerMedia. Free.
Play with the low latency view (full screen): the picture appears about 9 to 19 ms after the original passthrough (RTX 5080). For fighting or rhythm games use the passthrough screen.

Requirements
- An AVerMedia capture card and AVerMedia Streaming Center (tested versions 1.8.22.2 and the latest 1.9.19.1, x64).
- Graphics card: RIFE and FSR 3 need Vulkan, AMD FSR Frame Generation needs DirectX 12, NVIDIA Video Frame Generation and DLSS Frame Generation need NVIDIA RTX 40/50.
  Minimum: AMD Radeon RX 6600 (1080p 60 fps to 120 fps), the lowest card tested.
  GeForce, from an approximate test (RTX 5080 clocked down to match each class, 11-14): FSR 3 generated fully down to
  RTX 2060 class, RIFE down to RTX 3070 / 4060 class (2070 / 3060 class 68%).
  On the RX 6600 RIFE 4.6 estimates motion at 1/4 size (slightly lower quality); FSR 3 needs "Keep GPU clock up" on.
  Slower GPUs are not supported: generation may fall short or quality drops.
- A monitor of 120Hz or more to see the generated frames.

Install
1. Install AVerMedia Streaming Center and close it.
2. Run ConsoleSmooth-Plugin-Setup-1.1.0.exe and press Install (or Update).
   Depending on the install folder, Windows may ask for administrator rights.
   The installer is not signed: if Windows SmartScreen warns, choose "More info" then "Run anyway".
   The installer adds "consolesmooth" to your user environment variable GST_TRACERS. From Streaming Center 1.9.19.1 on,
   this is how the plugin starts. Other programs are not affected; uninstalling removes it and keeps any other value.
3. Press Done and start Streaming Center with its usual icon.
4. Two buttons appear on the toolbar: "Turn frame generation on" and "Frame generation settings". Press the first, or the shortcut (default Ctrl+Alt+F), to turn it on (it starts off).
   The settings window chooses and saves with Apply; choices take effect when you turn it on (applied while on: at the next turn-on).
   The second opens the settings window.

Settings (frame generation settings window)
- Input fps is detected from the capture card. Output is 2-4x or a target fps.
  The multiplier is of the input signal: a 30fps game still arrives at 60 Hz, so 2x outputs 120 Hz. How many new
  pictures fill it is set by 30fps smoothing (see "30fps games").
  Processing follows the Streaming Center recording resolution; the output size does not change.
- Model (six): RIFE 4.6 (default, balance of quality and speed) / RIFE 4.15 lite (a little better, a little heavier) / FSR 3 (2x and 4x, lighter; 4x splits each gap in the middle twice, no 3x) /
  AMD FSR Frame Generation (AMD DLL, 2x, lightest) / NVIDIA Video Frame Generation (RTX 40/50, best quality; the runtime is downloaded from NVIDIA the first time) /
  NVIDIA DLSS Frame Generation (RTX 40/50, 2x, 3x/4x on RTX 50; the NVIDIA runtime is in the installer).
- Latency: Low latency or Stable, and the generation buffer slider (15-25 ms). A larger buffer adds latency.
  The fast path of the low latency view (below) does not use this buffer. Recommended settings pick the buffer from a speed test.
- 30fps smoothing (off by default): makes 30fps games "game fps × multiplier", evenly spaced (see "30fps games"). About +8 ms latency.
- HUD protection (on by default): keeps fixed HUD (health bars, menus, semi-transparent button labels) from shimmering.
  In third-person games where the character stands still in the middle (GTA etc.) it can take the character for HUD and break
  the hair edge. Turn it off there.
  Also turn it off if letters break when a see-through text box (help, menus) or an in-game web page moves.
- FPS display: shows "input / now fps" by default (capture signal / frames shown per second). With "Show the game's real frame rate" on it shows "original / now fps" (e.g. 30 / 60 fps). Original = new game pictures per second,
  now = original + generated. A still picture reads 0 / 0. The first 2 s after turning on show signal / output fps.
- Latency priority (under Latency, off by default, RIFE only): computes motion at 1/4 size for a slightly smaller buffer (RTX 5080: 16.2 ms) at about 0.9 dB lower quality.
- Recording source: Original or With generated frames, from the next recording. With generated: recording and streaming run at the output rate from the start, so generation can be turned on later (pictures repeat while it is off).
- Sharpen full screen (display tab, off by default): sharper upscale and sharpening in full screen.
- Display: Window or Full screen (Esc closes it and turns frame generation off; turning it on again opens full screen). Full screen has less display latency.
- Low latency view (off by default): when turned on with the button or shortcut, shows the incoming picture in full screen right away. Recording and streaming go on while it is shown.
  With low latency view + full screen + Low latency + 2x, frames are made on the graphics card and sent straight to the screen (fast path, about 9-19 ms after the original).
  The fast path is chosen when Streaming Center starts: after changing the model or display settings, restart Streaming Center as the settings window says.
  If the fast path fails on a PC, this is remembered and the next start uses the standard path (choosing the model again retries). Laptops with two GPUs use the standard path.
- Display sync (window and full screen): V-Sync (default, no tearing) / Variable refresh (even on G-Sync/FreeSync monitors; the window needs NVIDIA "windowed and full screen" G-Sync) / Off (lowest delay, full screen may tear).
- On/off shortcut (Advanced tab, default Ctrl+Alt+F): click it and press a new combination with Ctrl or Alt; Backspace = no shortcut, Esc = cancel.
- Recommended: tries RIFE and FSR 3 on your PC for about 25 s and suggests model, multiplier, latency, generation buffer (15-25 ms) and display sync; RIFE only with enough headroom. Shows the GPU name, and suggests Variable refresh when the monitor appears to support it.
- Keep GPU clock up (on by default for AMD): keeps the GPU from lowering its clock while generating.
- Export diagnostics: saves one text file to send when something goes wrong. Nothing is sent automatically.
- Recording with generated frames: raise the Streaming Center recording video bitrate to 40000-50000 kbps; 120 fps at the default 15000 smears detail.

30fps games (e.g. PS5 GTA V quality mode, Bloodborne)
- The console sends every picture twice at 60 Hz. The readout shows "30 / …".
- 30fps smoothing off, 2x: 60 new pictures, lowest latency, but they alternate about 8 ms and 25 ms apart.
- 30fps smoothing on, 2x: 60 new pictures every 16.7 ms (30 -> 60); 3x gives 90, 4x 120. The middle picture needs the next
  original (33 ms later), so the buffer grows by about 8 ms (measured 25.0 -> 33.4 ms).
- Recording works the same way: the file is saved at the output rate (60 Hz x 2 = 120 fps), but for a 30fps game only 60 of those pictures per second are new; the rest repeat the previous picture (measured: file 119.9 fps, 60 new pictures/s).
- Motion between originals is twice that of a 60fps game, so fine detail such as a face can wobble on a sudden head turn
  (a limit of the model).

Language
The plugin follows the Streaming Center language: Korean when Streaming Center runs in Korean,
English otherwise. Without that setting it follows the Windows display language.

Limits
- Very fast camera turns can make the background shimmer slightly (a limit of the frame generation model).
- HDR/10-bit and 8K are not supported. Input is 30-120 fps (29.97/59.94/119.88 included). 4K: in engine measurements on an RTX 5080, RIFE 2x fits (quarter-size motion, about 14 ms per frame), 3x and more fall short; 1080p is recommended. Turn HDR off on PS5 and similar consoles. 59.94 Hz input (PS5 "60 Hz") is supported.
- If the GPU is too slow the target may not be reached and earlier frames can repeat.

Uninstall
Close Streaming Center and run Uninstall.exe in the plugin folder.
The original app and your settings are kept.

Source code and license
MIT. Third-party parts: see THIRD_PARTY_NOTICES.txt and the licenses folder.
Not made or endorsed by AVerMedia; no manufacturer files are included.

Problem reports
Attach the file from "Export diagnostics": https://github.com/GamKim/consolesmooth/issues

NVIDIA DLSS runtime and AMD FidelityFX frame generation DLLs are included under their own licenses (license files in engine\engines\dlssg and engine\engines\amdfg).
This software uses NVIDIA DLSS. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation.
