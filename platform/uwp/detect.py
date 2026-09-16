import os
import subprocess
import sys
from typing import TYPE_CHECKING

from methods import print_error

if TYPE_CHECKING:
    from misc.utility.scons_hints import SConsEnvironment

# UWP (HoloLens 2) port. Deliberately the least that boots: arm64 MSVC, WINAPI_FAMILY_APP,
# no rendering driver, no audio driver, no process/registry/console access in the OS layer.
# `scons platform=uwp target=template_debug` from any shell: SCons runs VsDevCmd.bat itself
# (-arch=arm64 -host_arch=x64 -app_platform=UWP) and builds in the environment it leaves.


def get_name():
    return "UWP"


def can_build():
    return os.name == "nt"


def _find_vs_dev_cmd():
    """VsDevCmd.bat of the newest Visual Studio (prerelease included) with the ARM64 C++ tools."""
    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio",
        "Installer",
        "vswhere.exe",
    )
    if not os.path.isfile(vswhere):
        return None
    out = subprocess.run(
        [
            vswhere,
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.ARM64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
    ).stdout.strip()
    if not out:
        return None
    devcmd = os.path.join(out, "Common7", "Tools", "VsDevCmd.bat")
    return devcmd if os.path.isfile(devcmd) else None


def get_tools(env: "SConsEnvironment"):
    # SCons' own MSVC detection does not see a prerelease Visual Studio and cannot ask for the
    # UWP tool set, so the environment comes from VsDevCmd.bat with the same arguments the
    # HoloLens probe builds with; SCons runs the script and takes the environment it leaves.
    devcmd = _find_vs_dev_cmd()
    if not devcmd:
        print_error(
            "The UWP platform needs Visual Studio with the ARM64 C++ build tools (Microsoft.VisualStudio.Component.VC.Tools.ARM64)."
        )
        sys.exit(255)
    args = "-arch=arm64 -host_arch=x64 -app_platform=UWP -no_logo"
    if env.get("mssdk_version"):
        args += " -winsdk=" + env["mssdk_version"]
    env["MSVC_USE_SCRIPT"] = devcmd
    env["MSVC_USE_SCRIPT_ARGS"] = args
    # Any version string keeps SCons from going looking for an install itself; the script decides.
    env["MSVC_VERSION"] = env["MSVS_VERSION"] = "14.5"
    env["TARGET_ARCH"] = "arm64"
    return ["msvc", "mslink", "mslib"]


def get_opts():
    return [
        ("mssdk_version", "Windows SDK version to use, e.g. 10.0.26100.0. Latest by default.", None),
    ]


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "supported": ["d3d12", "mono"],
        # No rendering, input or audio drivers yet; a later card adds D3D12 on a CoreWindow.
        "vulkan": False,
        "opengl3": False,
        "d3d12": False,
        "accesskit": False,
        "sdl": False,
        "xaudio2": False,
        "builtin_openxr": False,
        # UWP has no editor, and a headless boot needs only scripts, a text server and a font engine.
        "modules_enabled_by_default": False,
        "module_gdscript_enabled": True,
        "module_text_server_fb_enabled": True,
        "module_freetype_enabled": True,
    }


def configure(env: "SConsEnvironment"):
    if env["arch"] != "arm64":
        print_error('Unsupported CPU architecture "%s" for UWP. Only "arm64" (HoloLens 2) is supported.' % env["arch"])
        sys.exit(255)
    if env["target"] == "editor":
        print_error("UWP has no editor target; use target=template_debug or template_release.")
        sys.exit(255)
    if env["library_type"] != "executable":
        print_error("UWP only builds an executable.")
        sys.exit(255)

    env.msvc = True

    ## Build type

    # UWP packages take the C++ runtime from the Microsoft.VCLibs framework, never statically.
    env.AppendUnique(CCFLAGS=["/MD"])
    env.Append(LINKFLAGS=["/SUBSYSTEM:WINDOWS", "/INCREMENTAL:NO", "/STACK:8388608"])

    ## Compile flags

    env.AppendUnique(CCFLAGS=["/fp:strict", "/Gd", "/GR", "/nologo", "/utf-8", "/bigobj"])
    env.Prepend(CPPPATH=["#platform/uwp", "#drivers/windows"])
    env.AppendUnique(
        CPPDEFINES=[
            "UWP_ENABLED",
            "WINDOWS_ENABLED",
            "TYPED_METHOD_BIND",
            "WIN32",
            "_WIN64",
            "NOMINMAX",
            "UNICODE",
            "_UNICODE",
            ("WINVER", "0x0A00"),
            ("_WIN32_WINNT", "0x0A00"),
            ("WINAPI_FAMILY", "WINAPI_FAMILY_APP"),
        ]
    )
    if env["debug_symbols"]:
        env.AppendUnique(CCFLAGS=["/Zi", "/FS"])
        env.AppendUnique(LINKFLAGS=["/DEBUG:FULL"])

    ## Link flags

    # Everything an app container may import is in the WindowsApp umbrella library; the
    # desktop import libraries (kernel32, user32, ...) must not be pulled in by default.
    env.Append(
        LINKFLAGS=[
            "/APPCONTAINER",
            "/MANIFEST:NO",
            "/NXCOMPAT",
            "/DYNAMICBASE",
            "/NODEFAULTLIB:kernel32.lib",
            "/NODEFAULTLIB:ole32.lib",
        ]
    )
    env.Append(LINKFLAGS=[p + env["LIBSUFFIX"] for p in ["WindowsApp", "ws2_32"]])

    env["lto"] = "none"
