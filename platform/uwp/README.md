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

Modules default to off with `gdscript`, `text_server_fb` and `freetype` on; `mono` works (below);
everything else is `module_<name>_enabled=yes` away, untested.

## C# (module_mono_enabled=yes)

An app container has no hostfxr, hostpolicy or coreclr and loads only DLLs that are inside its
package, so C# on the HoloLens is exactly one thing: the project published under NativeAOT as a
shared library, `data_<name>_uwp_arm64\<name>.dll` beside `godot.exe`. What makes that happen:

- `modules/mono`: `gd_mono.cpp` skips the hostfxr/coreclr probes on UWP and goes to
  `try_load_native_aot_library()`; `godotsharp_dirs.cpp` maps `UWP` to `uwp` and always uses the
  beside-the-exe data dir (an extracted copy in LocalCache could never be loaded);
  `path_utils.cpp` reaches `CreateFileW` through `drivers/windows/app_container_windows.h`.
  `modules/mono/glue` is untouched, so the template's API hashes are the stock 4.7.2 editor's.
- `Godot.NET.Sdk`: `Sdk/UWP.props` and `Sdk/UWP.targets`, imported for
  `GodotTargetPlatform=uwp`: `PublishAot`, `NativeLib=Shared`, `UseUwp`, `/APPCONTAINER`, the
  game assembly and GodotSharp rooted for the trimmer, and the target framework forced to
  `net10.0-windows10.0.26100` (a project already on a windows10 TFM keeps its own). The publish is
  the editor's usual `dotnet publish -c Export* -r win-arm64 --self-contained true
  -p:GodotTargetPlatform=uwp`; ILCompiler finds the arm64 linker with vswhere, which must be on
  PATH (the VS installer directory).
- `GodotTools`: `OS.cs` knows the `UWP`/`uwp` platform (RID OS `win`), `ExportPlugin.cs` accepts
  it and never embeds the publish output in the pck; each file is handed to the export platform
  as a shared object under `data_<name>_uwp_arm64/`. The UWP `EditorExportPlatform` (E5) has to
  write those into the package next to the executable, as `EditorExportPlatformPC` does.

Neither the stock editor's `GodotTools.dll` nor the NuGet `Godot.NET.Sdk/4.7.2` has these, so
exporting from an editor needs this checkout's `GodotTools.dll` dropped into the editor's
`GodotSharp/Tools/` and the Sdk from `modules/mono/editor/Godot.NET.Sdk` on a feed — a packaging
question for the CI card, not answered here.

    scons platform=uwp target=template_debug module_mono_enabled=yes
    pwsh platform/uwp/deploy/build.ps1 -DotNet
    pwsh platform/uwp/deploy/run.ps1 -Password <device portal password>

packs `deploy/project-dotnet` (a `_Ready` in `Main.cs` that prints a `BOOT:` line and quits) and
its NativeAOT library, and the headset's log ends in
`BOOT: C# on Godot 4.7.2-stable (custom_build), UWP 10.0.22621.1560 (HoloLens 2), NativeAOT, 10.0.2`.
The library is ~30 MB (the .NET 10 runtime and GodotSharp, untrimmed) and takes a minute to build.

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
