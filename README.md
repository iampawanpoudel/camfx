# CamFX

Lightweight camera effects for Windows 11, similar to the macOS video effects. CamFX adds a camera called **CamFX Camera** that works in any app: Zoom, Teams, Google Meet, Discord, OBS, the Camera app. It shows your real webcam with effects applied:

- **Zoom and pan**: crop in instead of showing the full wide view
- **Auto-frame (follow me)**: keeps your head and shoulders centered as you move
- **Background blur** (5 strengths) or **background image**, using a person-segmentation model with edge refinement
- **Filters**: Mono, Warm, Cool, Vivid, Sepia, Fade
- **Adjustments**: brightness, contrast, saturation, warmth, exposure, low-light boost
- **Mirror**

It costs nothing when no app is using the camera. The effects only run while an app has CamFX Camera open, and then they use a few percent of one CPU core.

## Requirements

| | |
|---|---|
| **Windows** | Windows 11 version 22H2 (build 22621) or later. Check with `winver`. Windows 10 isn't supported: CamFX uses the virtual camera API that Windows 11 added, which is also why no driver is needed. |
| **PC** | 64-bit Intel or AMD (x64). Windows on ARM PCs (Snapdragon) aren't supported. |
| **Webcam** | Any webcam Windows recognizes: built-in or USB. |
| **CPU** | Background blur or image replacement uses about a fifth of one CPU core at 15 fps, and about 40% at 30 fps. Zoom, filters and adjustments cost almost nothing. |
| **Disk** | About 25 MB. |
| **Permissions** | Administrator rights to install or uninstall. Using it afterwards doesn't need them. |
| **Apps** | Any app that lets you choose a camera: Zoom, Microsoft Teams, Google Meet or other browser calls, Discord, Skype, OBS, the Windows Camera app. |

Nothing else needs to be installed: no Visual C++ redistributable, .NET, Python or drivers.

## Install

1. Download `CamFX-win-x64.zip` from the [latest release](../../releases/latest) and extract it to any folder.
2. Double-click **`Install.cmd`** and approve the administrator prompt.
   If Windows SmartScreen says "Windows protected your PC", click **More info → Run anyway**. CamFX isn't code-signed.
3. The CamFX window opens. Pick your webcam in the **Camera** list. The status line should say **"CamFX Camera ready"**.

You can delete the extracted folder afterwards. CamFX is installed to `C:\Program Files\CamFX` and appears in the Start menu.

Installing restarts the Windows camera service, so close video calls first.

## Use

1. In your call app's video settings, choose **CamFX Camera** as the camera. You only need to do this once per app (see [Set it once](#set-it-once)).
2. Open CamFX from the Start menu or the tray icon and change the effects. Changes apply live, including during a call.
   - The preview shows exactly what the other people in the call see.
   - **Show this panel when a call starts** opens CamFX automatically when an app turns on CamFX Camera.
   - **Start with Windows** keeps the tray icon available after a reboot. CamFX Camera itself keeps working without the tray app running.
3. Closing the window only hides it in the tray. Right-click the tray icon → **Exit** to quit.

Settings are stored in `C:\ProgramData\CamFX\settings.ini`.

### Set it once

Windows has no system-wide default camera; each app picks its own. Choose CamFX Camera once in each app, and the app remembers it:

| App | Where |
|---|---|
| Teams, Zoom, Discord, Skype, OBS | Settings → Video / Devices → Camera |
| Chrome / Edge (Google Meet and other web calls) | `chrome://settings/content/camera` or `edge://settings/content/camera`: choose CamFX Camera at the top. It becomes the default for every site. |
| Firefox | Choose it in the permission prompt and tick **Remember this decision** |
| Windows Camera app | Remembers the last camera you switched to |

**Leave CamFX Camera selected all the time.** When you don't want effects, click **Reset all** in CamFX. CamFX Camera then shows your plain webcam picture and skips all processing, so you never need to switch back to the webcam in your apps.

Don't disable or hide the real webcam (in Device Manager, or Settings → Cameras). CamFX reads its picture from it, so CamFX Camera would stop working too.

## Uninstall

Double-click **`Uninstall.cmd`** from the zip (or run `scripts\install.ps1 -Uninstall` from a source checkout). This removes the CamFX Camera device, the program files and the autostart entry. Your settings stay in `C:\ProgramData\CamFX`; delete that folder too if you don't need them.

## Troubleshooting

**Every camera app freezes or shows no image, even with the normal webcam, or the mic in call apps also stops working.**
The Windows camera service (Frame Server) is stuck. Close all camera apps, then run this in an **administrator** PowerShell:

```powershell
taskkill /F /IM CamFX.exe; taskkill /F /FI "SERVICES eq FrameServer"
```

The service starts again by itself the next time an app opens a camera. Restarting Windows also fixes it.

**CamFX Camera doesn't appear in my call app.** Open CamFX and check that the status line says "CamFX Camera ready", then restart the call app. Only the Windows user who installed CamFX sees the camera.

**"Could not register CamFX Camera".** Check that you're on Windows 11 22H2 or later (`winver`).

**The image is black or frozen.** Another app may hold the real webcam exclusively. Close it, or pick the webcam again in CamFX.

**Errors.** Look in Event Viewer → Windows Logs → Application for errors that mention `CamFXCamera.dll`.

## Developer guide

### Prerequisites

- Windows 11 22H2 or later, x64, with a webcam. You need the webcam to test the live camera; the unit tests run without one.
- **Visual Studio 2022** (Community or the free Build Tools) with the **Desktop development with C++** workload. Its recommended components include MSVC, CMake, Ninja and the Windows 11 SDK (10.0.22621 or newer). To install only the Build Tools from a terminal:
  ```powershell
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  ```
- Git, and internet access for the first build.

You don't need to install CMake or Ninja separately. `scripts\build.cmd` finds Visual Studio and uses the copies that ship with it.

### Build

```bat
git clone <this repo> && cd <repo folder>
scripts\build.cmd                 :: Release build into build\, then runs the unit tests
scripts\build.cmd RelWithDebInfo  :: optimized with debug symbols, into build-RelWithDebInfo\
scripts\build.cmd Debug           :: into build-Debug\
```

The first build runs `scripts\fetch-deps.ps1`, which downloads ONNX Runtime 1.30.0 and WIL into `third_party\` (git-ignored). Delete `third_party\` to download them again.

Build outputs in `build\`:

| File | What |
|---|---|
| `CamFXCamera.dll` | The media source that Windows Frame Server loads |
| `CamFX.exe` | Tray and settings app |
| `onnxruntime.dll`, `person_segmenter.onnx` | Copied next to the DLL at build time |
| `camfx_tests.exe` | Unit tests and benchmark |
| `camfx_grab.exe` | Reads frames from a camera the way a call app does |

### Install your build

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1            # self-elevates (UAC prompt)
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Uninstall
```

The installer stops CamFX.exe and **restarts the Windows Frame Server service**, which drops any camera stream that's open. Frame Server must restart: once it has loaded `CamFXCamera.dll`, it keeps using that copy even after the file is replaced. Then the installer copies the files to `C:\Program Files\CamFX` and registers the DLL with `regsvr32`. Start `CamFX.exe` once afterwards so it registers CamFX Camera with Windows.

If you work from an elevated terminal, the installer runs without a UAC prompt.

### Test

**Unit tests** (no camera needed). These run automatically at the end of every `build.cmd`:

```bat
build\camfx_tests.exe build          :: the folder argument adds the real ONNX model test
```

From a "Developer PowerShell for VS 2022", `ctest --test-dir build --output-on-failure` runs the same tests.

They cover the colour lookup tables, zoom/pan rectangles, passthrough, mirror + zoom, background blur and replace with fixed masks, and loading and running the segmentation model.

**Benchmark** of the effects pipeline on a synthetic 720p frame:

```bat
build\camfx_tests.exe build --bench
```

Treat its numbers as relative. A laptop on battery, or a camera stream running at the same time, changes them a lot.

**Live camera tests.** Install your build first, then:

```bat
build\camfx_grab.exe out.bmp 90                      :: 90 frames from CamFX Camera, saves the last one
build\camfx_grab.exe raw.bmp 30 "2M USB Camera"      :: the raw webcam (use your webcam's name, a prefix is enough)
```

It prints the cameras it finds, the format and the measured fps. Compare `raw.bmp` with `out.bmp` to check an effect.

To test an effect without clicking through the panel, edit `C:\ProgramData\CamFX\settings.ini`. The DLL checks the file every 30 frames, so changes apply within one or two seconds. The file is **UTF-16**, so keep that encoding when you change it from a script:

```powershell
$f = 'C:\ProgramData\CamFX\settings.ini'
(Get-Content $f -Encoding Unicode) -replace '^background=\d', 'background=1' | Set-Content $f -Encoding Unicode
```

| Key (`[CamFX]` section) | Values |
|---|---|
| `zoom` | 1 to 4 |
| `panX`, `panY` | -1 to 1 |
| `autoFrame`, `mirror`, `lowLight` | 0 or 1 |
| `background` | 0 off, 1 blur, 2 image (`C:\ProgramData\CamFX\background.img`) |
| `blur` | 1 (lightest) to 5 (strongest) |
| `brightness`, `contrast`, `saturation`, `warmth`, `exposure` | -100 to 100 |
| `filter` | 0 none, 1 mono, 2 warm, 3 cool, 4 vivid, 5 sepia, 6 fade |

The `[App]` section belongs to CamFX.exe: the chosen webcam, preview and popup options.

**Before you open a pull request**, check by hand:
- CamFX Camera shows up and streams in the Windows Camera app and in one call app.
- Blur and image background keep the whole person and none of the room.
- Your normal webcam still works in other apps while CamFX Camera is in use.
- Install over an existing install while a camera app is open, then uninstall.

### Debug

- `CamFXCamera.dll` runs **inside the Frame Server service** (`svchost.exe`, as LocalService), not inside CamFX.exe. To debug it, build `RelWithDebInfo` and copy everything in `build-RelWithDebInfo\` over `build\`. Then run `install.ps1`, open a camera app, and attach a debugger as administrator to the svchost process that hosts the service. Find its process ID with `tasklist /svc /fi "services eq FrameServer"`.
- Messages from the DLL (for example, the model failed to load) go to `OutputDebugString`. Read them with Sysinternals DebugView, run as administrator with **Capture Global Win32** turned on.
- Crashes show up in Event Viewer → Windows Logs → Application as "Faulting module name: CamFXCamera.dll".
- A crash or deadlock in the DLL can freeze every camera on the PC. Recover with `taskkill /F /FI "SERVICES eq FrameServer"` from an admin terminal.
- Frame Server can't read your user profile or HKCU. That's why settings live in `C:\ProgramData\CamFX`, and why the DLL loads `onnxruntime.dll` from its own folder by full path (Windows also ships an older `onnxruntime.dll` in System32).

### Code notes

- `vcam/Effects.cpp` has no COM or Media Foundation code, so the unit tests can drive it directly. It works in place on NV12 frames.
- `Segmenter::kW` and `kH` set the model input size, and `Effects::kMaskW`/`kMaskH` follow them. To swap the model, change `Segmenter.cpp`, the file name in `CMakeLists.txt`, `scripts\install.ps1` and `scripts\package.ps1`, and add the model's license to `models\`.
- Both targets use the static C++ runtime (`/MT`), so a release needs no VC++ redistributable. Keep it that way.

### Release

See [RELEASING.md](RELEASING.md) for building the zip and publishing a GitHub release.

## How it works

```
Webcam ──► Windows Frame Server ──► CamFXCamera.dll (effects) ──► "CamFX Camera" ──► call app
                                          ▲
CamFX.exe (tray + settings) ── settings.ini (C:\ProgramData\CamFX)
```

- `CamFX.exe` registers CamFX Camera with `MFCreateVirtualCamera` and links it to the webcam you pick. Windows remembers the registration across reboots.
- When an app opens CamFX Camera, Windows Frame Server loads `CamFXCamera.dll`, a Media Foundation media source based on Microsoft's [VirtualCamera sample](https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera). It wraps the webcam and runs the effects on every NV12 frame. When the app closes the camera, the DLL stops.
- Background effects use the PP-HumanSegV2-Lite portrait model (256x144) through ONNX Runtime on the CPU (1 thread, every frame). A guided filter then refines the mask edges at full resolution.

| Path | What |
|---|---|
| `vcam/` | Media source DLL. `Effects.cpp` has the effects pipeline, `Segmenter.cpp` the ONNX model, `Settings.cpp` the settings file |
| `app/` | CamFX.exe, a Win32 tray app with the settings panel and preview |
| `tests/` | Effects unit tests (`effects_test.cpp`), frame grabber (`grab.cpp`) |
| `scripts/` | Build, install, uninstall and packaging scripts |
| `models/` | `person_segmenter.onnx` (PP-HumanSegV2-Lite portrait 256x144 from PaddleSeg, converted with paddle2onnx at a fixed input shape) |

## License

CamFX is released under the [MIT License](LICENSE).

It includes third-party parts under their own licenses:

- `vcam/` is derived from Microsoft's Windows-Camera samples (MIT, see `vcam/LICENSE-Microsoft.txt`).
- ONNX Runtime (MIT).
- PP-HumanSegV2-Lite model by PaddlePaddle (Apache-2.0, see `models/LICENSE-PP-HumanSeg.txt`).
