How to uninstall ConsoleSmooth

1. Close Streaming Center.
2. In File Explorer, open the Plugin\ConsoleSmooth folder inside the Streaming Center install folder.
   Default: C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center\Plugin\ConsoleSmooth
3. Run Uninstall.exe and press "Uninstall". Choose "Yes" if Windows asks for administrator rights.
4. When it says the uninstall finished, you are done. Streaming Center starts as before next time.

Removed
- The files this installer added (plugin, engine, model, notes).
- The consolesmooth entry in your user environment variable GST_TRACERS. Other entries are kept.

Kept
- Streaming Center itself.
- Your ConsoleSmooth settings: %LOCALAPPDATA%\ConsoleSmoothNative
- Files you changed, and the uninstaller itself.
- You can delete the leftover Plugin\ConsoleSmooth folder and the settings folder by hand.

Notes
- Do not delete the whole Streaming Center folder or other plugin folders.
- If you uninstall or reinstall Streaming Center first, the plugin folder may stay behind: run its Uninstall.exe or delete the folder.
- If the uninstaller reports changed files, it kept the files you changed and removed the rest.
