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

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

static int godot_main() {
	OS_UWP os;

	// The project is godot.pck beside godot.exe in the package (see deploy/build.ps1), which is
	// the exe-named pck ProjectSettings looks for by itself; path overrides are compiled out.
	CharString exe_utf8 = os.get_executable_path().utf8();
	const char *args[] = { "--verbose" };

	Error err = Main::setup(exe_utf8.get_data(), std::size(args), (char **)args);
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
