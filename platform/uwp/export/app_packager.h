/**************************************************************************/
/*  app_packager.h                                                        */
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

#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include <zlib.h>

// Writes an appx: a zip whose central directory is zip64, plus the AppxBlockMap.xml (a SHA-256
// per 64 KiB block of every file) and [Content_Types].xml that makeappx would generate. A file
// is streamed from disk or given as a buffer; each is deflated or stored block by block behind
// a local header whose sizes and CRC are patched once the data is through, so nothing is held
// in memory but one block.
class AppxPackager {
	enum {
		FILE_HEADER_MAGIC = 0x04034b50,
		CENTRAL_DIR_MAGIC = 0x02014b50,
		END_OF_CENTRAL_DIR_MAGIC = 0x06054b50,
		ZIP64_END_OF_CENTRAL_DIR_MAGIC = 0x06064b50,
		ZIP64_END_DIR_LOCATOR_MAGIC = 0x07064b50,
		ZIP_VERSION = 20,
		ZIP_ARCHIVE_VERSION = 45,
		GENERAL_PURPOSE = 0x0800, // File names are UTF-8.
		BASE_FILE_HEADER_SIZE = 30,
		BASE_CENTRAL_DIR_SIZE = 46,
		ZIP64_END_OF_CENTRAL_DIR_SIZE = (56 - 12),
		END_OF_CENTRAL_DIR_SIZE = 42,
		BLOCK_SIZE = 65536,
	};

	struct BlockHash {
		String base64_hash;
		uint64_t compressed_size = 0;
	};

	struct FileMeta {
		CharString name; // UTF-8, forward slashes.
		int lfh_size = 0;
		bool compressed = false;
		uint64_t compressed_size = 0;
		uint64_t uncompressed_size = 0;
		Vector<BlockHash> hashes;
		uLong file_crc32 = 0;
		uint64_t zip_offset = 0;
	};

	// One source of file data: a buffer or an open file.
	struct Source {
		const uint8_t *buffer = nullptr;
		Ref<FileAccess> file;
		uint64_t size = 0;
		uint64_t read(uint8_t *p_dst, uint64_t p_len);
	};

	Ref<FileAccess> package;
	Vector<FileMeta> file_metadata;
	uint64_t central_dir_offset = 0;
	uint64_t end_of_central_dir_offset = 0;
	Vector<uint8_t> central_dir_data;

	static String hash_block(const uint8_t *p_block_data, size_t p_block_len);
	static String content_type(const String &p_extension);
	String make_block_map() const;
	String make_content_types() const;

	Vector<uint8_t> make_file_header(const FileMeta &p_file_meta) const;
	void store_central_dir_header(const FileMeta &p_file);
	Vector<uint8_t> make_end_of_central_record() const;

	Error _add(const String &p_file_name, Source &p_source, bool p_compress);

public:
	void init(Ref<FileAccess> p_fa);
	Error add_file(const String &p_file_name, const uint8_t *p_buffer, size_t p_len, bool p_compress);
	Error add_file(const String &p_file_name, const String &p_source_path, bool p_compress);
	Error finish();
};
