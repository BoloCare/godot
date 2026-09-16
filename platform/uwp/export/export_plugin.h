/**************************************************************************/
/*  export_plugin.h                                                       */
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

#include "app_packager.h"

#include "editor/export/editor_export_platform.h"

class ImageTexture;

// Exports an appx for the HoloLens 2 (UWP, arm64): the template zip's godot.exe, manifest and
// logos, the project as godot.pck beside the exe, the shared objects export plugins add (the C#
// NativeAOT library under data_<name>_uwp_arm64/), and the command line in __cl__.cl, which
// platform/uwp/godot_uwp.cpp reads. Signed with signtool from the Windows SDK.
class EditorExportPlatformUWP : public EditorExportPlatform {
	GDCLASS(EditorExportPlatformUWP, EditorExportPlatform);

	Ref<ImageTexture> logo;

	static String _find_signtool();
	static bool _valid_identity_name(const String &p_name);
	static bool _valid_bgcolor(const String &p_color);
	bool _valid_image(const Ref<EditorExportPreset> &p_preset, const String &p_option, int p_width, int p_height, String &r_error) const;

	String _fix_manifest(const Ref<EditorExportPreset> &p_preset, const String &p_template) const;
	Vector<uint8_t> _command_line_file(const Ref<EditorExportPreset> &p_preset, BitField<EditorExportPlatform::DebugFlags> p_flags);
	Error _add_shared_object(AppxPackager &p_packager, const String &p_source, const String &p_target);
	Error _sign(const Ref<EditorExportPreset> &p_preset, const String &p_path);

public:
	virtual String get_name() const override { return "UWP"; }
	virtual String get_os_name() const override { return "UWP"; }
	virtual Ref<Texture2D> get_logo() const override;

	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;
	virtual void get_export_options(List<ExportOption> *r_options) const override;
	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override;
	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override;
	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0, bool p_notify = true) override;
	virtual void get_platform_features(List<String> *r_features) const override;

	EditorExportPlatformUWP();
};
