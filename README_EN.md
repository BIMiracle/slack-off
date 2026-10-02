# SlackOff

**Lightweight window hiding for Windows 11 · A native C++ / Win32 desktop tool**

[中文](README.md) | English

[![Platform: Windows 11](https://img.shields.io/badge/Platform-Windows%2011-0078D4.svg)](https://github.com/BIMiracle/slack-off)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)
[![Portable](https://img.shields.io/badge/Portable-Single%20EXE-green.svg)](#installation-and-usage)

Hide or restore all visible windows belonging to a selected program with global shortcuts, or automatically hide a video window when the cursor leaves it. The hide and watching shortcuts distinguish left and right Ctrl and Alt. Runs as a single EXE without .NET, a browser engine, or an installed Visual C++ runtime.

> Hiding windows does not stop background audio, network activity, or notifications. Configure a pre-hide key to pause playback. Hiding another program's own system tray icons is currently unsupported.

## Features

- **Hide and restore**: Match programs by their full EXE path and control all running instances at that path. Press the same shortcut again to restore windows and their original positions.
- **Left and right shortcuts**: Record left Ctrl, right Ctrl, left Alt, and right Alt independently for hiding and watching mode. Use different modifier sides with the same main key for different actions.
- **Watching mode**: Resize the target window manually, enable the mode, and move the cursor inside. The first movement outside its visible frame hides it. After restoration, monitoring waits until the cursor enters again.
- **Pre-hide key**: Send a pause key such as Space and configure a delay of 0–500 ms. Restoration does not send a playback key.
- **Tray and startup**: Closing or minimizing settings keeps the tool in the tray. Supports login startup for the current user, restore all, and restart as administrator.
- **Recovery and monitoring**: Restore windows before normal exit and attempt recovery on the next launch after a crash. Continue hiding new or redisplayed target windows while a rule is hidden.

## Installation and Usage

### 1. Prerequisites

Run on **Windows 11 x64**. Building requires Visual Studio 2022 / 2026 with **Desktop development with C++**, the Windows SDK, and CMake. The application has no additional runtime dependencies.

### 2. Build and launch

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script builds Release, runs CTest, and copies the EXE and both READMEs to `release\`. Place `release\SlackOff.exe` in a permanent location and run it. The first launch opens settings and enables login startup for the current user by default. Login startup opens only the tray icon.

### 3. Configure a program and shortcuts

The settings interface is in Chinese; the labels below identify the corresponding controls.

1. Select a program from the window list and click **选用程序** (Use program), or click **浏览 EXE** (Browse EXE). A rule controls all instances using the selected executable path.
2. Click **隐藏 / 恢复快捷键** (Hide / restore shortcut) and press a combination such as `Left Ctrl+Left Alt+F9`.
3. Optionally set **看剧模式快捷键** (Watching shortcut), such as `Right Ctrl+Right Alt+F10`, and **隐藏前按键** (Pre-hide key), such as Space. Click **保存程序** (Save program).
4. Press the hide shortcut to hide all currently visible target windows; press it again to restore them. Double-clicking a rule also toggles visibility. The tray menu offers restore all and exit.
5. For watching, resize the window first, press the watching shortcut, then move the cursor into the target window. Leaving its visible frame hides it. Press the watching shortcut again to disable the mode.

| Setting | Default / behavior |
| --- | --- |
| Hide / restore shortcut | Unset; required before saving a program |
| Watching shortcut | Unset; optional |
| Pre-hide key | Unset; optional |
| Delay after key | `50 ms`; configurable from `0–500 ms` |
| Restore all shortcut | `Ctrl+Alt+Shift+F12`, either side; configurable in global settings |
| Login startup | Enabled on first normal launch; current-user permissions |

### 4. Left and right Ctrl / Alt

The hide and watching fields display the modifier sides actually recorded, as does the rule list. The Chinese labels `左` and `右` mean left and right. Shift accepts either side, and the interface does not record Win modifiers. Backspace / Delete clears a field; Tab / Shift+Tab changes focus.

| Recorded shortcut | Trigger behavior |
| --- | --- |
| `Left Ctrl+Left Alt+F9` | Only this combination triggers; right Ctrl or right Alt does not |
| `Right Ctrl+Right Alt+F9` | Can share F9 with the preceding combination for a different action |
| Both Ctrl or Alt sides held while recording | Both recorded sides must also be held when triggering |
| Legacy `Ctrl+Alt+F9` | Continues accepting either side; record and save it again to restrict sides |

Recording a shortcut does not execute the combination being recorded. Holding the main key triggers once; release and press it again to repeat. Duplicate or overlapping bindings are rejected. For example, a legacy generic `Ctrl+Alt+F9` overlaps a new `Left Ctrl+Left Alt+F9`. Failed saves preserve the previous working shortcuts.

Windows reserves the entire generic `Ctrl+Alt+main key` combination. SlackOff distinguishes sides internally, so another program reserving the generic combination still prevents registration. Avoid system-reserved shortcuts. Restore all and pre-hide key fields retain generic Ctrl and Alt behavior.

## Compatibility Boundaries

- Hides ordinary desktop windows and their running taskbar buttons. **Another program's own tray icons are unsupported**. SlackOff keeps its own tray entry; pinned taskbar launchers remain present.
- Does not close or suspend target processes. Background audio, networking, and notifications need to be paused or disabled in the target application.
- Matches full executable paths, not individual browser tabs. All matching browser windows are affected. UWP, protected windows, exclusive fullscreen, special input programs, and different privilege levels need separate validation.
- Sends the pre-hide key only during manual or cursor-exit hiding. Waits up to 500 ms for shortcut modifiers to be released and sends only after confirming that the target has foreground focus. If confirmation fails, it still hides and reports “按键发送未确认” (Key delivery unconfirmed). Accepted system input does not prove that playback paused.
- Runs with ordinary permissions by default. For elevated targets, including scrcpy launched as administrator, use **以管理员身份重启** (Restart as administrator), confirm the Windows prompt, then use the shortcuts. Hiding and enabling watching mode check the target's integrity level first and report when elevation is needed. Hidden windows are restored before restarting. Login startup does not request elevation.
- Uses physical screen coordinates and Per-Monitor V2 DPI awareness. The mouse hook is installed only during watching mode. A keyboard hook tracks modifier sides and records shortcuts. Neither uses background polling.
- Recovery validates PID, process creation time, executable path, window class, and a window property token to avoid restoring reused handles. Windows that were already hidden are not shown.
- Windows events are asynchronous. New windows may appear briefly before being hidden; universal flicker-free behavior is not guaranteed.

## Settings and Startup

| Item | Location |
| --- | --- |
| Settings | `%LOCALAPPDATA%\SlackOff\settings.ini` |
| Crash recovery journal | `%LOCALAPPDATA%\SlackOff\recovery.ini` |
| Current-user startup | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SlackOff` |

Settings and recovery journals are written to a temporary file and atomically replaced. The journal contains executable paths and window identities, not window contents. Settings format v2 stores modifier sides and accepts legacy two-field shortcuts.

The startup entry contains the EXE's absolute path plus `--tray`. After moving the EXE, disable and re-enable startup to update the path. To remove the tool, disable startup, exit from the tray, and delete the EXE. Relaunch it to recover windows left hidden after an abnormal exit.

## Build and Tests

Uses the static runtime `/MT`. Release enables `/O1`, LTCG, and unused-code removal.

```powershell
pwsh -File build.ps1
pwsh -File tests/integration.ps1
pwsh -File tests/shortcuts.ps1
```

Exit any existing SlackOff instance from its tray before testing so its global shortcuts are released. The `--self-test` run by `build.ps1` covers persistence, modifier-side matching, transactional shortcut registration, multiple-window hiding, key delivery, and recovery. It does not modify real user settings or startup.

`tests/integration.ps1` validates the application state machine. `tests/shortcuts.ps1` injects system keyboard input to test left/right combinations, rejection of wrong sides, shared main keys, and repeat suppression. Both use isolated test instances. Injected keyboard events still do not replace physical-keyboard acceptance testing.

Additional checks: `pwsh -File tests/measure.ps1` measures resources. `pwsh -File tests/real-apps.ps1` checks actual applications and needs an existing FFmpeg / FFplay installation to play a temporary test video. It skips already running user programs of the same name. FFmpeg is not an application runtime dependency.

Native reports are written to `%TEMP%\SlackOff-test-<PID>\results.txt`; application reports use `%TEMP%\SlackOff-UI-test\`. `--test-instance` uses isolated settings without real startup registration. `--fixture` supplies two visible windows and one initially hidden window. See [VALIDATION.md (Chinese)](VALIDATION.md) for measurements and validation boundaries.

## Project Structure

```text
slack-off/
├── src/
│   ├── main.cpp            # Chinese settings UI, tray, and state machine
│   ├── hotkeys.cpp         # Modifier sides, keyboard hook, registration transactions
│   ├── platform.cpp        # Window identities, key injection, and boundary checks
│   ├── settings.cpp        # Atomic persistence, startup, and crash recovery
│   └── app.h               # Data model and module interfaces
├── resources/              # Windows resources and DPI / privilege manifest
├── tests/                  # Application, shortcuts, real-program, and resource tests
├── CMakeLists.txt          # MSVC / Windows SDK build configuration
├── build.ps1               # Build, CTest, and output packaging
├── README.md               # Chinese README (default)
├── README_EN.md            # English README
└── VALIDATION.md           # Measurements and compatibility boundaries
```

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=BIMiracle/slack-off&type=Date)](https://star-history.com/#BIMiracle/slack-off&Date)
