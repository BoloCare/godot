/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/marshalls.h"
#include "core/io/zip_io.h"
#include "core/os/os.h"
#include "core/os/shared_object.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/image_texture.h"

#include "modules/modules_enabled.gen.h" // For svg.
#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif

#include "thirdparty/minizip/unzip.h"

// Manifest capabilities, each an export option under capabilities/. The HoloLens 2 has no use
// for the rest of the UWP list. The uap2 one needs its namespace, which the template declares.
static const char *uwp_capabilities[] = {
	"internetClient",
	"internetClientServer",
	"privateNetworkClientServer",
	nullptr
};
static const char *uwp_uap2_capabilities[] = {
	"spatialPerception",
	nullptr
};
static const char *uwp_device_capabilities[] = {
	"microphone",
	"webcam",
	"bluetooth",
	"location",
	"gazeInput",
	nullptr
};

// The logos in the template zip, and the option and size of the one that replaces each.
struct UWPImage {
	const char *file;
	const char *option;
	int width;
	int height;
};
static const UWPImage uwp_images[] = {
	{ "Assets/StoreLogo.png", "images/store_logo", 50, 50 },
	{ "Assets/Square44x44Logo.png", "images/square44x44_logo", 44, 44 },
	{ "Assets/Square150x150Logo.png", "images/square150x150_logo", 150, 150 },
	{ "Assets/SplashScreen.png", "images/splash_screen", 620, 300 },
};

// Environment variables that override the signing options, for a CI that keeps the certificate
// out of export_presets.cfg.
static const String ENV_UWP_SIGNING_CERT = "GODOT_UWP_SIGNING_CERTIFICATE";
static const String ENV_UWP_SIGNING_PASS = "GODOT_UWP_SIGNING_PASSWORD";

static const char *TEMPLATE_DEBUG = "uwp_arm64_debug.zip";
static const char *TEMPLATE_RELEASE = "uwp_arm64_release.zip";

String EditorExportPlatformUWP::_find_signtool() {
	// The newest Windows 10/11 SDK's x64 signtool.
	String kits = OS::get_singleton()->get_environment("ProgramFiles(x86)").path_join("Windows Kits/10/bin");
	Ref<DirAccess> da = DirAccess::open(kits);
	if (da.is_null()) {
		return String();
	}
	String newest;
	da->list_dir_begin();
	for (String d = da->get_next(); !d.is_empty(); d = da->get_next()) {
		if (da->current_is_dir() && d.begins_with("10.") && d > newest && FileAccess::exists(kits.path_join(d).path_join("x64/signtool.exe"))) {
			newest = d;
		}
	}
	da->list_dir_end();
	return newest.is_empty() ? String() : kits.path_join(newest).path_join("x64/signtool.exe");
}

bool EditorExportPlatformUWP::_valid_identity_name(const String &p_name) {
	// Package identity: 3 to 50 of [A-Za-z0-9.-], no leading, trailing or doubled dot.
	if (p_name.length() < 3 || p_name.length() > 50 || p_name.begins_with(".") || p_name.ends_with(".") || p_name.contains("..")) {
		return false;
	}
	for (int i = 0; i < p_name.length(); i++) {
		char32_t c = p_name[i];
		if (!is_ascii_alphanumeric_char(c) && c != '.' && c != '-') {
			return false;
		}
	}
	return true;
}

bool EditorExportPlatformUWP::_valid_bgcolor(const String &p_color) {
	if (p_color.is_empty() || p_color == "transparent") {
		return true;
	}
	return p_color.begins_with("#") && p_color.length() == 7 && p_color.is_valid_html_color();
}

bool EditorExportPlatformUWP::_valid_image(const Ref<EditorExportPreset> &p_preset, const String &p_option, int p_width, int p_height, String &r_error) const {
	String path = p_preset->get(p_option);
	if (path.is_empty()) {
		return true;
	}
	if (path.get_extension().to_lower() != "png" || !FileAccess::exists(path)) {
		r_error += vformat(TTR("%s: \"%s\" is not a PNG file."), p_option, path) + "\n";
		return false;
	}
	Ref<Image> img = Image::load_from_file(path);
	if (img.is_null() || img->get_width() != p_width || img->get_height() != p_height) {
		r_error += vformat(TTR("%s: \"%s\" should be %dx%d."), p_option, path, p_width, p_height) + "\n";
		return false;
	}
	return true;
}

List<String> EditorExportPlatformUWP::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("appx");
	return list;
}

void EditorExportPlatformUWP::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	if (p_preset->get("texture_format/s3tc_bptc")) {
		r_features->push_back("s3tc");
		r_features->push_back("bptc");
	}
	if (p_preset->get("texture_format/etc2_astc")) {
		r_features->push_back("etc2");
		r_features->push_back("astc");
	}
	r_features->push_back(p_preset->get("binary_format/architecture"));
}

void EditorExportPlatformUWP::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "binary_format/architecture", PROPERTY_HINT_ENUM, "arm64"), "arm64"));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "texture_format/s3tc_bptc"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "texture_format/etc2_astc"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "command_line/extra_args"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/display_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Game Name"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/unique_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Company.GameName"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/description"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/publisher", PROPERTY_HINT_PLACEHOLDER_TEXT, "CN=CompanyName"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/publisher_display_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Company Name"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/major", PROPERTY_HINT_RANGE, "0,65535,1"), 1));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/minor", PROPERTY_HINT_RANGE, "0,65535,1"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/build", PROPERTY_HINT_RANGE, "0,65535,1"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/revision", PROPERTY_HINT_RANGE, "0,65535,1"), 0));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "images/background_color", PROPERTY_HINT_PLACEHOLDER_TEXT, "transparent or #rrggbb"), "transparent"));
	for (const UWPImage &image : uwp_images) {
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, image.option, PROPERTY_HINT_FILE, "*.png"), ""));
	}

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "signing/certificate", PROPERTY_HINT_GLOBAL_FILE, "*.pfx", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "signing/password", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));

	for (const char **c = uwp_capabilities; *c; c++) {
		// Network both ways by default: the HoloLens is reached over its USB NIC.
		bool on = String(*c) == "internetClient" || String(*c) == "internetClientServer";
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "capabilities/" + String(*c)), on));
	}
	for (const char **c = uwp_uap2_capabilities; *c; c++) {
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "capabilities/" + String(*c)), false));
	}
	for (const char **c = uwp_device_capabilities; *c; c++) {
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "capabilities/" + String(*c)), false));
	}
}

bool EditorExportPlatformUWP::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	String err;

	bool dvalid = exists_export_template(TEMPLATE_DEBUG, &err);
	bool rvalid = exists_export_template(TEMPLATE_RELEASE, &err);

	if (p_preset->get("custom_template/debug") != "") {
		dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
		if (!dvalid) {
			err += TTR("Custom debug template not found.") + "\n";
		}
	}
	if (p_preset->get("custom_template/release") != "") {
		rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
		if (!rvalid) {
			err += TTR("Custom release template not found.") + "\n";
		}
	}

	bool valid = dvalid || rvalid;
	r_missing_templates = !valid;

	if (!err.is_empty()) {
		r_error = err;
	}
	return valid;
}

bool EditorExportPlatformUWP::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	if (!_valid_identity_name(p_preset->get("package/unique_name"))) {
		valid = false;
		err += TTR("Invalid package unique name: 3 to 50 letters, digits, dots and dashes (Company.GameName).") + "\n";
	}
	if (!String(p_preset->get("package/publisher")).begins_with("CN=")) {
		valid = false;
		err += TTR("Invalid package publisher: must be the signing certificate's subject (CN=...).") + "\n";
	}
	if (String(p_preset->get("package/publisher_display_name")).strip_edges().is_empty()) {
		valid = false;
		err += TTR("Package publisher display name is empty.") + "\n";
	}
	if (!_valid_bgcolor(p_preset->get("images/background_color"))) {
		valid = false;
		err += TTR("Invalid background color: transparent or #rrggbb.") + "\n";
	}
	for (const UWPImage &image : uwp_images) {
		valid = _valid_image(p_preset, image.option, image.width, image.height, err) && valid;
	}

	r_error = err;
	return valid;
}

String EditorExportPlatformUWP::_fix_manifest(const Ref<EditorExportPreset> &p_preset, const String &p_template) const {
	String result = p_template;

	String display_name = p_preset->get("package/display_name");
	if (display_name.is_empty()) {
		display_name = GLOBAL_GET("application/config/name");
	}
	String version = itos(p_preset->get("version/major")) + "." + itos(p_preset->get("version/minor")) + "." + itos(p_preset->get("version/build")) + "." + itos(p_preset->get("version/revision"));

	result = result.replace("$godot_version$", GODOT_VERSION_FULL_NAME);
	result = result.replace("$identity_name$", String(p_preset->get("package/unique_name")).xml_escape(true));
	result = result.replace("$publisher$", String(p_preset->get("package/publisher")).xml_escape(true));
	result = result.replace("$version_string$", version);
	result = result.replace("$architecture$", p_preset->get("binary_format/architecture"));
	result = result.replace("$display_name$", display_name.xml_escape(true));
	result = result.replace("$publisher_display_name$", String(p_preset->get("package/publisher_display_name")).xml_escape(true));
	result = result.replace("$app_description$", String(p_preset->get("package/description")).xml_escape(true));
	result = result.replace("$bg_color$", String(p_preset->get("images/background_color")).is_empty() ? String("transparent") : String(p_preset->get("images/background_color")));

	String capabilities;
	for (const char **c = uwp_capabilities; *c; c++) {
		if ((bool)p_preset->get("capabilities/" + String(*c))) {
			capabilities += "    <Capability Name=\"" + String(*c) + "\" />\n";
		}
	}
	for (const char **c = uwp_uap2_capabilities; *c; c++) {
		if ((bool)p_preset->get("capabilities/" + String(*c))) {
			capabilities += "    <uap2:Capability Name=\"" + String(*c) + "\" />\n";
		}
	}
	for (const char **c = uwp_device_capabilities; *c; c++) {
		if ((bool)p_preset->get("capabilities/" + String(*c))) {
			capabilities += "    <DeviceCapability Name=\"" + String(*c) + "\" />\n";
		}
	}
	result = result.replace("$capabilities_place$", capabilities.is_empty() ? String("<Capabilities />") : "<Capabilities>\n" + capabilities + "  </Capabilities>");

	return result;
}

Vector<uint8_t> EditorExportPlatformUWP::_command_line_file(const Ref<EditorExportPreset> &p_preset, BitField<EditorExportPlatform::DebugFlags> p_flags) {
	// uint32 argc, then per argument uint32 length + UTF-8; godot_uwp.cpp reads it back.
	Vector<String> args;
	for (const String &arg : String(p_preset->get("command_line/extra_args")).strip_edges().split(" ", false)) {
		args.push_back(arg);
	}
	args.append_array(gen_export_flags(p_flags));

	Vector<uint8_t> clf;
	clf.resize(4);
	encode_uint32(args.size(), clf.ptrw());
	for (const String &arg : args) {
		CharString utf8 = arg.utf8();
		int base = clf.size();
		clf.resize(base + 4 + utf8.length());
		encode_uint32(utf8.length(), &clf.write[base]);
		memcpy(&clf.write[base + 4], utf8.get_data(), utf8.length());
	}
	return clf;
}

Error EditorExportPlatformUWP::_add_shared_object(AppxPackager &p_packager, const String &p_source, const String &p_target) {
	// A shared object may be a directory (an export plugin's whole output); its files keep their layout.
	Ref<DirAccess> da = DirAccess::open(p_source);
	if (da.is_valid()) {
		da->list_dir_begin();
		for (String n = da->get_next(); !n.is_empty(); n = da->get_next()) {
			if (n == "." || n == "..") {
				continue;
			}
			Error err = _add_shared_object(p_packager, p_source.path_join(n), p_target.path_join(n));
			if (err != OK) {
				return err;
			}
		}
		da->list_dir_end();
		return OK;
	}
	return p_packager.add_file(p_target, p_source, true);
}

Error EditorExportPlatformUWP::_sign(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
	String cert_path = p_preset->get_or_env("signing/certificate", ENV_UWP_SIGNING_CERT);
	String cert_pass = p_preset->get_or_env("signing/password", ENV_UWP_SIGNING_PASS);
	if (cert_path.is_empty()) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("No signing certificate set: the package is unsigned and will not install on a device until it is signed."));
		return OK;
	}
	if (!FileAccess::exists(cert_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("Could not find certificate file at \"%s\"."), cert_path));
		return ERR_FILE_NOT_FOUND;
	}

	String signtool_path = EDITOR_GET("export/uwp/signtool");
	if (signtool_path.is_empty()) {
		signtool_path = _find_signtool();
	}
	if (signtool_path.is_empty() || !FileAccess::exists(signtool_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), TTR("Could not find signtool. Install a Windows SDK, or set its path in the Editor Settings (Export > UWP > signtool)."));
		return ERR_FILE_NOT_FOUND;
	}

	List<String> args;
	args.push_back("sign");
	args.push_back("/fd");
	args.push_back("SHA256");
	args.push_back("/f");
	args.push_back(cert_path);
	if (!cert_pass.is_empty()) {
		args.push_back("/p");
		args.push_back(cert_pass);
	}
	args.push_back(p_path);

	String output;
	int exit_code = 0;
	Error err = OS::get_singleton()->execute(signtool_path, args, &output, &exit_code, true);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("Could not start signtool at \"%s\"."), signtool_path));
		return err;
	}
	if (exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("signtool failed to sign the package: %s"), output.strip_edges()));
		return FAILED;
	}
	return OK;
}

Error EditorExportPlatformUWP::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);

	String template_path = p_preset->get(p_debug ? "custom_template/debug" : "custom_template/release");
	template_path = template_path.strip_edges();
	if (template_path.is_empty()) {
		String err;
		template_path = find_export_template(p_debug ? TEMPLATE_DEBUG : TEMPLATE_RELEASE, &err);
		if (template_path.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), err);
			return ERR_FILE_NOT_FOUND;
		}
	}

	if (!DirAccess::exists(p_path.get_base_dir())) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Export path \"%s\" does not exist."), p_path.get_base_dir()));
		return ERR_FILE_BAD_PATH;
	}

	EditorProgress ep("export", TTR("Exporting for UWP"), 5, true);

	// The project as a pck, from which save_pack also collects the shared objects export plugins added.
	if (ep.step(TTR("Packing project files..."), 0)) {
		return ERR_SKIP;
	}
	String pck_path = EditorPaths::get_singleton()->get_cache_dir().path_join("uwp_export.pck");
	Vector<SharedObject> so_files;
	Error err = save_pack(p_preset, p_debug, pck_path, &so_files);
	if (err != OK) {
		DirAccess::remove_file_or_error(pck_path);
		return err;
	}

	if (ep.step(TTR("Creating package..."), 1)) {
		DirAccess::remove_file_or_error(pck_path);
		return ERR_SKIP;
	}
	Ref<FileAccess> fa_pack = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Creating Package"), vformat(TTR("Could not create file \"%s\"."), p_path));
		DirAccess::remove_file_or_error(pck_path);
		return ERR_CANT_CREATE;
	}
	AppxPackager packager;
	packager.init(fa_pack);

	// The template: godot.exe, the manifest with the preset filled in, and the logos the preset does not replace.
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(template_path.utf8().get_data(), &io);
	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not open export template \"%s\"."), template_path));
		DirAccess::remove_file_or_error(pck_path);
		return ERR_FILE_NOT_FOUND;
	}
	int ret = unzGoToFirstFile(pkg);
	while (ret == UNZ_OK && err == OK) {
		unz_file_info info;
		char fname[16384];
		ret = unzGetCurrentFileInfo(pkg, &info, fname, sizeof(fname), nullptr, 0, nullptr, 0);
		if (ret != UNZ_OK) {
			break;
		}
		String path = String::utf8(fname);
		if (path.ends_with("/")) {
			ret = unzGoToNextFile(pkg);
			continue;
		}

		String image_source;
		for (const UWPImage &image : uwp_images) {
			if (path == image.file) {
				image_source = p_preset->get(image.option);
			}
		}

		if (!image_source.is_empty()) {
			err = packager.add_file(path, ProjectSettings::get_singleton()->globalize_path(image_source), false);
		} else {
			Vector<uint8_t> data;
			data.resize(info.uncompressed_size);
			unzOpenCurrentFile(pkg);
			unzReadCurrentFile(pkg, data.ptrw(), data.size());
			unzCloseCurrentFile(pkg);

			if (path == "AppxManifest.xml") {
				CharString manifest = _fix_manifest(p_preset, String::utf8((const char *)data.ptr(), data.size())).utf8();
				err = packager.add_file(path, (const uint8_t *)manifest.get_data(), manifest.length(), true);
			} else {
				err = packager.add_file(path, data.ptr(), data.size(), path.get_extension() != "png");
			}
		}
		ret = unzGoToNextFile(pkg);
	}
	unzClose(pkg);

	if (err == OK && ep.step(TTR("Adding project files..."), 2)) {
		err = ERR_SKIP;
	}
	if (err == OK) {
		err = packager.add_file("godot.pck", pck_path, false);
	}
	for (int i = 0; i < so_files.size() && err == OK; i++) {
		String src_path = ProjectSettings::get_singleton()->globalize_path(so_files[i].path);
		// GodotTools gives the target as a Windows path ending in "\.", so normalize it.
		String target = so_files[i].target.replace("\\", "/").simplify_path().path_join(src_path.get_file()).trim_prefix("/");
		err = _add_shared_object(packager, src_path, target);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Shared Objects"), vformat(TTR("Failed to add shared object \"%s\"."), src_path));
		}
	}
	if (err == OK) {
		Vector<uint8_t> clf = _command_line_file(p_preset, p_flags);
		err = packager.add_file("__cl__.cl", clf.ptr(), clf.size(), false);
	}

	if (err == OK && ep.step(TTR("Closing package..."), 3)) {
		err = ERR_SKIP;
	}
	if (err == OK) {
		err = packager.finish();
	}
	fa_pack.unref();
	DirAccess::remove_file_or_error(pck_path);
	if (err != OK) {
		if (err != ERR_SKIP) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Creating Package"), vformat(TTR("Failed to write \"%s\"."), p_path));
		}
		DirAccess::remove_file_or_error(p_path);
		return err;
	}

	if (ep.step(TTR("Signing package..."), 4)) {
		return ERR_SKIP;
	}
	return _sign(p_preset, p_path);
}

void EditorExportPlatformUWP::get_platform_features(List<String> *r_features) const {
	r_features->push_back("uwp");
}

Ref<Texture2D> EditorExportPlatformUWP::get_logo() const {
	return logo;
}

EditorExportPlatformUWP::EditorExportPlatformUWP() {
#ifdef MODULE_SVG_ENABLED
	Ref<Image> img = memnew(Image);
	const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

	ImageLoaderSVG::create_image_from_string(img, _uwp_logo_svg, EDSCALE, upsample, false);
	logo = ImageTexture::create_from_image(img);
#endif
}
