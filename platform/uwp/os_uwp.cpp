/**************************************************************************/
/*  os_uwp.cpp                                                            */
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

#include "os_uwp.h"

#include "display_server_uwp.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/logger.h"
#include "core/os/main_loop.h"
#include "drivers/windows/dir_access_windows.h"
#include "drivers/windows/file_access_windows.h"
#include "drivers/windows/ip_windows.h"
#include "drivers/windows/net_socket_winsock.h"
#include "drivers/windows/thread_windows.h"
#include "main/main.h"

#include <bcrypt.h>
#include <stdio.h>

// An app container has no console: stdout/stderr go to the debugger (visible in the Device
// Portal's ETW view as OutputDebugString events) and to LocalState\godot.log, which the Device
// Portal's file API can read back over the cable.
class UWPLogger : public Logger {
	FILE *file = nullptr;

public:
	explicit UWPLogger(const String &p_path) {
		_wfopen_s(&file, (const wchar_t *)p_path.utf16().get_data(), L"a");
	}

	virtual void logv(const char *p_format, va_list p_list, bool p_err) override {
		if (!should_log(p_err)) {
			return;
		}
		char buf[4096];
		int len = vsnprintf(buf, sizeof(buf), p_format, p_list);
		if (len < 0) {
			return;
		}
		OutputDebugStringA(buf);
		if (file) {
			fputs(buf, file);
			fflush(file);
		}
	}

	virtual ~UWPLogger() {
		if (file) {
			fclose(file);
		}
	}
};

void OS_UWP::initialize() {
#ifdef THREADS_ENABLED
	init_thread_win();
#endif

	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_USERDATA);
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_FILESYSTEM);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_USERDATA);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_FILESYSTEM);

	NetSocketWinSock::make_default();
	IPWindows::make_default();

	QueryPerformanceFrequency((LARGE_INTEGER *)&ticks_per_second);
	QueryPerformanceCounter((LARGE_INTEGER *)&ticks_start);
}

void OS_UWP::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

void OS_UWP::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

void OS_UWP::finalize() {
	delete_main_loop();
}

void OS_UWP::finalize_core() {
	FileAccessWindows::finalize();
	NetSocketWinSock::cleanup();
}

Error OS_UWP::get_entropy(uint8_t *r_buffer, int p_bytes) {
	NTSTATUS status = BCryptGenRandom(nullptr, r_buffer, p_bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	ERR_FAIL_COND_V(status, FAILED);
	return OK;
}

// Only DLLs inside the package can be loaded, and only by LoadPackagedLibrary; the path is
// relative to the package root (a GDExtension's `res://` DLL ends up there at export).
Error OS_UWP::open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data) {
	String path = p_path.replace_char('/', '\\');
	if (path.begins_with(install_path)) {
		path = path.substr(install_path.length()).trim_prefix("\\");
	}
	p_library_handle = (void *)LoadPackagedLibrary((LPCWSTR)(path.utf16().get_data()), 0);
	ERR_FAIL_NULL_V_MSG(p_library_handle, ERR_CANT_OPEN, vformat("Can't open dynamic library: %s. Error: %d.", p_path, GetLastError()));
	if (p_data != nullptr && p_data->r_resolved_path != nullptr) {
		*p_data->r_resolved_path = install_path.path_join(path);
	}
	return OK;
}

Error OS_UWP::close_dynamic_library(void *p_library_handle) {
	if (!FreeLibrary((HMODULE)p_library_handle)) {
		return FAILED;
	}
	return OK;
}

Error OS_UWP::get_dynamic_library_symbol_handle(void *p_library_handle, const String &p_name, void *&p_symbol_handle, bool p_optional) {
	p_symbol_handle = (void *)GetProcAddress((HMODULE)p_library_handle, p_name.utf8().get_data());
	if (!p_symbol_handle) {
		if (!p_optional) {
			ERR_FAIL_V_MSG(ERR_CANT_RESOLVE, vformat("Can't resolve symbol %s, error: %d.", p_name, GetLastError()));
		} else {
			return ERR_CANT_RESOLVE;
		}
	}
	return OK;
}

MainLoop *OS_UWP::get_main_loop() const {
	return main_loop;
}

String OS_UWP::get_name() const {
	return "UWP";
}

String OS_UWP::get_distribution_name() const {
	return "Windows Holographic";
}

String OS_UWP::get_version() const {
	return UWPWinRT::device_family_version();
}

Vector<String> OS_UWP::get_video_adapter_driver_info() const {
	return Vector<String>();
}

OS::DateTime OS_UWP::get_datetime(bool p_utc) const {
	SYSTEMTIME systemtime;
	if (p_utc) {
		GetSystemTime(&systemtime);
	} else {
		GetLocalTime(&systemtime);
	}

	TIME_ZONE_INFORMATION info;
	bool is_daylight = false;
	if (!p_utc && GetTimeZoneInformation(&info) == TIME_ZONE_ID_DAYLIGHT) {
		is_daylight = true;
	}

	DateTime dt;
	dt.year = systemtime.wYear;
	dt.month = Month(systemtime.wMonth);
	dt.day = systemtime.wDay;
	dt.weekday = Weekday(systemtime.wDayOfWeek);
	dt.hour = systemtime.wHour;
	dt.minute = systemtime.wMinute;
	dt.second = systemtime.wSecond;
	dt.dst = is_daylight;
	return dt;
}

OS::TimeZoneInfo OS_UWP::get_time_zone_info() const {
	TIME_ZONE_INFORMATION info;
	bool is_daylight = false;
	if (GetTimeZoneInformation(&info) == TIME_ZONE_ID_DAYLIGHT) {
		is_daylight = true;
	}

	TimeZoneInfo ret;
	if (is_daylight) {
		ret.name = info.DaylightName;
		ret.bias = info.Bias + info.DaylightBias;
	} else {
		ret.name = info.StandardName;
		ret.bias = info.Bias + info.StandardBias;
	}
	// GetTimeZoneInformation's bias is the inverse of what we expect (GMT-3 gives 180).
	ret.bias = -ret.bias;
	return ret;
}

double OS_UWP::get_unix_time() const {
	// 1 Windows tick is 100ns.
	const uint64_t WINDOWS_TICKS_PER_SECOND = 10000000;
	const uint64_t TICKS_TO_UNIX_EPOCH = 116444736000000000LL;

	SYSTEMTIME st;
	GetSystemTime(&st);
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft);
	uint64_t ticks_time;
	ticks_time = ft.dwHighDateTime;
	ticks_time <<= 32;
	ticks_time |= ft.dwLowDateTime;

	return (double)(ticks_time - TICKS_TO_UNIX_EPOCH) / WINDOWS_TICKS_PER_SECOND;
}

void OS_UWP::delay_usec(uint32_t p_usec) const {
	if (p_usec < 1000) {
		Sleep(1);
	} else {
		Sleep(p_usec / 1000);
	}
}

uint64_t OS_UWP::get_ticks_usec() const {
	uint64_t ticks;
	QueryPerformanceCounter((LARGE_INTEGER *)&ticks);
	ticks -= ticks_start;

	// Split the division to keep it from overflowing after days of uptime.
	uint64_t seconds = ticks / ticks_per_second;
	uint64_t leftover = ticks % ticks_per_second;
	return (leftover * 1000000L) / ticks_per_second + seconds * 1000000L;
}

bool OS_UWP::has_environment(const String &p_var) const {
	return GetEnvironmentVariableW((LPCWSTR)(p_var.utf16().get_data()), nullptr, 0) > 0;
}

String OS_UWP::get_environment(const String &p_var) const {
	WCHAR wval[0x7fff]; // MSDN says 32767 char is the maximum.
	int wlen = GetEnvironmentVariableW((LPCWSTR)(p_var.utf16().get_data()), wval, 0x7fff);
	if (wlen > 0) {
		return String::utf16((const char16_t *)wval);
	}
	return "";
}

void OS_UWP::set_environment(const String &p_var, const String &p_value) const {
	ERR_FAIL_COND_MSG(p_var.is_empty() || p_var.contains_char('='), vformat("Invalid environment variable name '%s', cannot be empty or include '='.", p_var));
	SetEnvironmentVariableW((LPCWSTR)(p_var.utf16().get_data()), (LPCWSTR)(p_value.utf16().get_data()));
}

void OS_UWP::unset_environment(const String &p_var) const {
	ERR_FAIL_COND_MSG(p_var.is_empty() || p_var.contains_char('='), vformat("Invalid environment variable name '%s', cannot be empty or include '='.", p_var));
	SetEnvironmentVariableW((LPCWSTR)(p_var.utf16().get_data()), nullptr);
}

String OS_UWP::get_executable_path() const {
	WCHAR buf[4096];
	GetModuleFileNameW(nullptr, buf, 4096);
	return String::utf16((const char16_t *)buf).replace_char('\\', '/');
}

String OS_UWP::get_locale() const {
	WCHAR name[LOCALE_NAME_MAX_LENGTH];
	if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0) {
		return "en";
	}
	return String::utf16((const char16_t *)name).replace_char('-', '_');
}

String OS_UWP::get_processor_name() const {
	// The registry is out of reach; the HoloLens 2's SoC is known.
	return "Qualcomm Snapdragon 850";
}

String OS_UWP::get_model_name() const {
	return UWPWinRT::device_model_name();
}

String OS_UWP::get_config_path() const {
	return local_state_path;
}

String OS_UWP::get_data_path() const {
	return local_state_path;
}

String OS_UWP::get_cache_path() const {
	return local_cache_path;
}

String OS_UWP::get_temp_path() const {
	return temp_state_path;
}

String OS_UWP::get_system_dir(SystemDir p_dir, bool p_shared_storage) const {
	// The user's libraries need capabilities this package does not declare; keep it in the sandbox.
	return local_state_path;
}

String OS_UWP::get_user_data_dir(const String &p_user_dir) const {
	// The package is the app: no per-project subdirectory under the sandbox is needed.
	return local_state_path;
}

String OS_UWP::get_unique_id() const {
	return String();
}

void OS_UWP::run() {
	if (!main_loop) {
		return;
	}

	main_loop->initialize();

	while (true) {
		DisplayServer::get_singleton()->process_events();
		if (Main::iteration()) {
			break;
		}
	}

	main_loop->finalize();
}

bool OS_UWP::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "uwp" || p_feature == "hololens") {
		return true;
	}
	return false;
}

OS_UWP::OS_UWP() {
	local_state_path = UWPWinRT::local_state_path();
	local_cache_path = UWPWinRT::local_cache_path();
	temp_state_path = UWPWinRT::temp_state_path();
	install_path = UWPWinRT::install_path();

	DisplayServerUWP::register_uwp_driver();

	Vector<Logger *> loggers;
	loggers.push_back(memnew(UWPLogger(local_state_path.path_join("godot.log"))));
	_set_logger(memnew(CompositeLogger(loggers)));
}

OS_UWP::~OS_UWP() {
}
