/**************************************************************************/
/*  godot_uwp.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// The UWP entry point: a C++/WinRT CoreApplication host whose IFrameworkView::Run() is the
// engine's main. The system calls Initialize/SetWindow/Load/Run on the thread it started the
// process on, and Godot's setup, main loop and cleanup all happen inside Run(), after the
// CoreWindow is activated; DisplayServerUWP pumps that window's dispatcher once per frame.
//
// StereoKit's platforms/uwp.cpp instead runs CoreApplication on a second thread and keeps the
// engine on the first. Tried here first: the HoloLens 2 (22621.1536) terminates the process
// within a second of the CoreWindow activating when the view is not on the startup thread, with
// no crash dump, so the view thread is the engine thread, as in the D3D12 probe.

#include "display_server_uwp.h"
#include "os_uwp.h"

#include "main/main.h"

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

// The command line the export wrote beside godot.exe: uint32 argc, then per argument a uint32
// length and its UTF-8 (platform/uwp/export/export_plugin.cpp). A package with no __cl__.cl
// (deploy/build.ps1) runs with none.
static std::vector<std::string> read_command_line(const std::wstring &p_path) {
	std::vector<std::string> args;
	FILE *f = _wfopen(p_path.c_str(), L"rb");
	if (!f) {
		return args;
	}
	auto read_u32 = [f](uint32_t &r_value) { return fread(&r_value, 4, 1, f) == 1; };
	uint32_t argc = 0;
	if (read_u32(argc) && argc < 1024) {
		for (uint32_t i = 0; i < argc; i++) {
			uint32_t len = 0;
			if (!read_u32(len) || len > 65536) {
				break;
			}
			std::string arg(len, '\0');
			if (fread(arg.data(), 1, len, f) != len) {
				break;
			}
			args.push_back(std::move(arg));
		}
	}
	fclose(f);
	return args;
}

static int godot_main() {
	OS_UWP os;

	// The project is godot.pck beside godot.exe in the package, which is the exe-named pck
	// ProjectSettings looks for by itself; __cl__.cl beside it carries the preset's extra
	// arguments and the editor's remote-debug flags.
	std::wstring exe_path(std::size_t(MAX_PATH), L'\0');
	exe_path.resize(GetModuleFileNameW(nullptr, exe_path.data(), (DWORD)exe_path.size()));
	std::vector<std::string> args = read_command_line(exe_path.substr(0, exe_path.find_last_of(L'\\') + 1) + L"__cl__.cl");
	std::vector<char *> argv;
	for (std::string &arg : args) {
		argv.push_back(arg.data());
	}

	CharString exe_utf8 = os.get_executable_path().utf8();
	Error err = Main::setup(exe_utf8.get_data(), (int)argv.size(), argv.data());
	if (err != OK) {
		return err == ERR_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();
	return os.get_exit_code();
}

struct GodotFrameworkView : implements<GodotFrameworkView, IFrameworkViewSource, IFrameworkView> {
	IFrameworkView CreateView() {
		return *this;
	}

	void Initialize(CoreApplicationView const &p_view) {
		p_view.Activated({ this, &GodotFrameworkView::_on_activated });
	}

	void Load(hstring const &) {}

	void SetWindow(CoreWindow const &p_window) {
		p_window.Closed([](auto &&, auto &&) { DisplayServerUWP::window_closed(); });
	}

	void Run() {
		CoreWindow::GetForCurrentThread().Activate();
		godot_main();
		CoreApplication::Exit();
	}

	void Uninitialize() {}

	void _on_activated(CoreApplicationView const &, IActivatedEventArgs const &) {
		CoreWindow::GetForCurrentThread().Activate();
	}
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	init_apartment();
	CoreApplication::Run(make<GodotFrameworkView>());
	return 0;
}
