# UWP platform port (HoloLens 2)

Upstream removed `platform/uwp` in 8de6405288 (Godot 4.2 dev); this is a fresh, minimal port for
the HoloLens 2, added back in the least amount that boots. It is not the old port revived.

What it is today:

- arm64 only, MSVC only, `WINAPI_FAMILY_APP`, linked `/APPCONTAINER` against `WindowsApp.lib`.
- `OS_UWP`: what `OS_Windows` does minus what an app container cannot do (child processes,
  registry, console, system fonts). Paths come from `ApplicationData` (`user://` is
  `LocalState`), libraries from `LoadPackagedLibrary`, stdout/stderr go to `OutputDebugString`
  and `LocalState\godot.log`.
- `DisplayServerUWP`: the headless display server under the platform's name, plus the
  CoreWindow dispatcher pump in `process_events()`. No rendering, no input.
- `godot_uwp.cpp`: a C++/WinRT `CoreApplication` / `IFrameworkView` host. Godot's setup, main
  loop and cleanup run inside `IFrameworkView::Run()` on the thread the system started the
  process on. (Running the view on a second thread, StereoKit style, gets the process
  terminated by the HoloLens within a second of the window activating.)
- `drivers/windows` compiles unchanged except for `app_container_windows.h`, which forwards
  `CreateFileW` and `MoveFileW` to their app-partition equivalents, no symlinks, and
  `is_case_sensitive()` answering false (ntdll is out of reach).

Modules default to off with `gdscript`, `text_server_fb` and `freetype` on; everything else is
`module_<name>_enabled=yes` away, untested.

## Building

    scons platform=uwp target=template_debug

from any shell: `detect.py` finds Visual Studio (prerelease included) with vswhere and has SCons
run `VsDevCmd.bat -arch=arm64 -host_arch=x64 -app_platform=UWP`. The Windows SDK's arm64
`Microsoft.VCLibs` appx and `makeappx`/`signtool` are needed for packaging.

## Running it on a HoloLens 2

    pwsh platform/uwp/deploy/build.ps1     # godot.pck from deploy/project, a signed godot.appx
    pwsh platform/uwp/deploy/run.ps1 -Password <device portal password>

`build.ps1` packs `deploy/project` (a scene whose `_ready` prints a `BOOT:` line and quits) with a
desktop Godot 4.7 through `PCKPacker`, lays out `godot.exe` + `godot.pck` + `AppxManifest.xml`,
and signs with a self-signed test certificate it makes on first use. `run.ps1` installs the appx
through Device Portal over the USB cable, launches it, and prints `LocalState\godot.log`.
