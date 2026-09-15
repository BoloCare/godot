/**************************************************************************/
/*  os_uwp.h                                                              */
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

#pragma once

#include "core/os/os.h"

#include <windows.h>

// The app-container OS: what OS_Windows does minus everything an app container cannot do
// (child processes, the registry, a console, the desktop's font and user directories).
// Paths come from the package's ApplicationData, libraries from LoadPackagedLibrary.
class OS_UWP : public OS {
	uint64_t ticks_start = 0;
	uint64_t ticks_per_second = 0;
	MainLoop *main_loop = nullptr;

	String local_state_path; // ApplicationData.Current.LocalFolder: user://, config, data, logs.
	String local_cache_path; // ApplicationData.Current.LocalCacheFolder.
	String temp_state_path; // ApplicationData.Current.TemporaryFolder.
	String install_path; // Package.Current.InstalledLocation: the exe and the pck.

protected:
	virtual void initialize() override;
	virtual void set_main_loop(MainLoop *p_main_loop) override;
	virtual void delete_main_loop() override;
	virtual void finalize() override;
	virtual void finalize_core() override;

	virtual String get_stdin_string(int64_t p_buffer_size = 1024) override { return String(); }
	virtual PackedByteArray get_stdin_buffer(int64_t p_buffer_size = 1024) override { return PackedByteArray(); }

public:
	virtual Error get_entropy(uint8_t *r_buffer, int p_bytes) override;

	virtual Error open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data = nullptr) override;
	virtual Error close_dynamic_library(void *p_library_handle) override;
	virtual Error get_dynamic_library_symbol_handle(void *p_library_handle, const String &p_name, void *&p_symbol_handle, bool p_optional = false) override;

	virtual MainLoop *get_main_loop() const override;

	virtual String get_name() const override;
	virtual String get_distribution_name() const override;
	virtual String get_version() const override;

	virtual Vector<String> get_video_adapter_driver_info() const override;

	virtual void initialize_joypads() override {}

	virtual DateTime get_datetime(bool p_utc) const override;
	virtual TimeZoneInfo get_time_zone_info() const override;
	virtual double get_unix_time() const override;

	virtual void delay_usec(uint32_t p_usec) const override;
	virtual uint64_t get_ticks_usec() const override;

	// No child processes in an app container.
	virtual Error execute(const String &p_path, const List<String> &p_arguments, String *r_pipe = nullptr, int *r_exitcode = nullptr, bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	virtual Error create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	virtual Error kill(const ProcessID &p_pid) override { return ERR_UNAVAILABLE; }
	virtual bool is_process_running(const ProcessID &p_pid) const override { return false; }
	virtual int get_process_exit_code(const ProcessID &p_pid) const override { return -1; }

	virtual bool has_environment(const String &p_var) const override;
	virtual String get_environment(const String &p_var) const override;
	virtual void set_environment(const String &p_var, const String &p_value) const override;
	virtual void unset_environment(const String &p_var) const override;

	virtual String get_executable_path() const override;
	virtual String get_locale() const override;
	virtual String get_processor_name() const override;
	virtual String get_model_name() const override;

	virtual String get_config_path() const override;
	virtual String get_data_path() const override;
	virtual String get_cache_path() const override;
	virtual String get_temp_path() const override;
	virtual String get_system_dir(SystemDir p_dir, bool p_shared_storage = true) const override;
	virtual String get_user_data_dir(const String &p_user_dir) const override;

	virtual String get_unique_id() const override;

	void run();

	virtual bool _check_internal_feature_support(const String &p_feature) override;

	OS_UWP();
	~OS_UWP();
};

// Implemented in uwp_winrt.cpp (C++/WinRT, compiled as C++20).
namespace UWPWinRT {
String local_state_path();
String local_cache_path();
String temp_state_path();
String install_path();
String device_family_version();
String device_model_name();
} // namespace UWPWinRT
