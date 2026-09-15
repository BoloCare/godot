/**************************************************************************/
/*  uwp_winrt.cpp                                                         */
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

// The few WinRT facts OS_UWP needs, kept out of os_uwp.cpp so that file compiles with the
// engine's own flags (C++17, no exceptions) while this one is C++/WinRT.

#include "os_uwp.h"

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Security.ExchangeActiveSyncProvisioning.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.Profile.h>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::Security::ExchangeActiveSyncProvisioning;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::System::Profile;

static String _to_string(const hstring &p_str) {
	return String::utf16((const char16_t *)p_str.c_str(), p_str.size());
}

String UWPWinRT::local_state_path() {
	return _to_string(ApplicationData::Current().LocalFolder().Path());
}

String UWPWinRT::local_cache_path() {
	return _to_string(ApplicationData::Current().LocalCacheFolder().Path());
}

String UWPWinRT::temp_state_path() {
	return _to_string(ApplicationData::Current().TemporaryFolder().Path());
}

String UWPWinRT::install_path() {
	return _to_string(Package::Current().InstalledLocation().Path());
}

// DeviceFamilyVersion is a uint64 in decimal: four 16-bit fields, e.g. 10.0.22621.1266.
String UWPWinRT::device_family_version() {
	uint64_t v = std::stoull(std::wstring(AnalyticsInfo::VersionInfo().DeviceFamilyVersion().c_str()));
	return vformat("%d.%d.%d.%d", (int)((v >> 48) & 0xFFFF), (int)((v >> 32) & 0xFFFF), (int)((v >> 16) & 0xFFFF), (int)(v & 0xFFFF));
}

String UWPWinRT::device_model_name() {
	EasClientDeviceInformation info;
	return _to_string(info.SystemProductName());
}
