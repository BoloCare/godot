/**************************************************************************/
/*  app_packager.cpp                                                      */
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

#include "app_packager.h"

#include "core/crypto/crypto_core.h"
#include "core/io/marshalls.h"
#include "core/io/zip_io.h"
#include "core/templates/hash_set.h"

uint64_t AppxPackager::Source::read(uint8_t *p_dst, uint64_t p_len) {
	if (file.is_valid()) {
		return file->get_buffer(p_dst, p_len);
	}
	memcpy(p_dst, buffer, p_len);
	buffer += p_len;
	return p_len;
}

String AppxPackager::hash_block(const uint8_t *p_block_data, size_t p_block_len) {
	unsigned char hash[32];
	CryptoCore::sha256(p_block_data, p_block_len, hash);
	return CryptoCore::b64_encode_str(hash, 32);
}

String AppxPackager::make_block_map() const {
	String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>";
	xml += "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\">";
	for (const FileMeta &file : file_metadata) {
		xml += "<File Name=\"" + String::utf8(file.name).replace("/", "\\").xml_escape(true) + "\" Size=\"" + itos(file.uncompressed_size) + "\" LfhSize=\"" + itos(file.lfh_size) + "\">";
		for (const BlockHash &block : file.hashes) {
			xml += "<Block Hash=\"" + block.base64_hash + "\"";
			if (file.compressed) {
				xml += " Size=\"" + itos(block.compressed_size) + "\"";
			}
			xml += "/>";
		}
		xml += "</File>";
	}
	xml += "</BlockMap>";
	return xml;
}

String AppxPackager::content_type(const String &p_extension) {
	if (p_extension == "png") {
		return "image/png";
	} else if (p_extension == "jpg") {
		return "image/jpeg";
	} else if (p_extension == "xml") {
		return "application/xml";
	} else if (p_extension == "exe" || p_extension == "dll") {
		return "application/x-msdownload";
	}
	return "application/octet-stream";
}

String AppxPackager::make_content_types() const {
	String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	xml += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">";

	HashSet<String> extensions;
	for (const FileMeta &file : file_metadata) {
		String name = String::utf8(file.name);
		String ext = name.get_extension().to_lower();
		if (ext.is_empty()) {
			// OPC has no default for an extensionless part; it needs an override of its own.
			xml += "<Override PartName=\"/" + name.xml_escape(true) + "\" ContentType=\"application/octet-stream\"/>";
		} else if (!extensions.has(ext)) {
			extensions.insert(ext);
			xml += "<Default Extension=\"" + ext.xml_escape(true) + "\" ContentType=\"" + content_type(ext) + "\"/>";
		}
	}

	// The signature signtool adds, and the package parts.
	xml += "<Default Extension=\"p7x\" ContentType=\"application/octet-stream\"/>";
	xml += "<Override PartName=\"/AppxManifest.xml\" ContentType=\"application/vnd.ms-appx.manifest+xml\"/>";
	xml += "<Override PartName=\"/AppxBlockMap.xml\" ContentType=\"application/vnd.ms-appx.blockmap+xml\"/>";
	xml += "<Override PartName=\"/AppxSignature.p7x\" ContentType=\"application/vnd.ms-appx.signature\"/>";
	xml += "<Override PartName=\"/AppxMetadata/CodeIntegrity.cat\" ContentType=\"application/vnd.ms-pkiseccat\"/>";
	xml += "</Types>";
	return xml;
}

Vector<uint8_t> AppxPackager::make_file_header(const FileMeta &p_file_meta) const {
	Vector<uint8_t> buf;
	buf.resize(BASE_FILE_HEADER_SIZE + p_file_meta.name.length());
	uint8_t *w = buf.ptrw();

	w += encode_uint32(FILE_HEADER_MAGIC, w);
	w += encode_uint16(ZIP_VERSION, w);
	w += encode_uint16(GENERAL_PURPOSE, w);
	w += encode_uint16(p_file_meta.compressed ? Z_DEFLATED : 0, w);
	w += encode_uint32(0, w); // Date and time.
	w += encode_uint32(p_file_meta.file_crc32, w);
	w += encode_uint32(p_file_meta.compressed_size, w);
	w += encode_uint32(p_file_meta.uncompressed_size, w);
	w += encode_uint16(p_file_meta.name.length(), w);
	w += encode_uint16(0, w); // Extra field length.
	memcpy(w, p_file_meta.name.get_data(), p_file_meta.name.length());

	return buf;
}

void AppxPackager::store_central_dir_header(const FileMeta &p_file) {
	Vector<uint8_t> &buf = central_dir_data;
	int offs = buf.size();
	buf.resize(buf.size() + BASE_CENTRAL_DIR_SIZE + p_file.name.length());
	uint8_t *w = buf.ptrw() + offs;

	w += encode_uint32(CENTRAL_DIR_MAGIC, w);
	w += encode_uint16(ZIP_ARCHIVE_VERSION, w);
	w += encode_uint16(ZIP_VERSION, w);
	w += encode_uint16(GENERAL_PURPOSE, w);
	w += encode_uint16(p_file.compressed ? Z_DEFLATED : 0, w);
	w += encode_uint32(0, w); // Date and time.
	w += encode_uint32(p_file.file_crc32, w);
	w += encode_uint32(p_file.compressed_size, w);
	w += encode_uint32(p_file.uncompressed_size, w);
	w += encode_uint16(p_file.name.length(), w);
	w += encode_uint16(0, w); // Extra field length.
	w += encode_uint16(0, w); // Comment length.
	w += encode_uint16(0, w); // Disk number start.
	w += encode_uint16(0, w); // Internal attributes.
	w += encode_uint32(0, w); // External attributes.
	w += encode_uint32(p_file.zip_offset, w);
	memcpy(w, p_file.name.get_data(), p_file.name.length());
}

Vector<uint8_t> AppxPackager::make_end_of_central_record() const {
	Vector<uint8_t> buf;
	buf.resize(ZIP64_END_OF_CENTRAL_DIR_SIZE + 12 + END_OF_CENTRAL_DIR_SIZE); // Size plus magic.
	uint8_t *w = buf.ptrw();

	// Zip64 end of central directory record.
	w += encode_uint32(ZIP64_END_OF_CENTRAL_DIR_MAGIC, w);
	w += encode_uint64(ZIP64_END_OF_CENTRAL_DIR_SIZE, w);
	w += encode_uint16(ZIP_ARCHIVE_VERSION, w);
	w += encode_uint16(ZIP_ARCHIVE_VERSION, w);
	w += encode_uint32(0, w); // This disk.
	w += encode_uint32(0, w); // Disk with the central directory.
	w += encode_uint64(file_metadata.size(), w); // Entries on this disk.
	w += encode_uint64(file_metadata.size(), w); // Entries in total.
	w += encode_uint64(central_dir_data.size(), w);
	w += encode_uint64(central_dir_offset, w);

	// Zip64 end of central directory locator.
	w += encode_uint32(ZIP64_END_DIR_LOCATOR_MAGIC, w);
	w += encode_uint32(0, w); // Disk with the zip64 record.
	w += encode_uint64(end_of_central_dir_offset, w);
	w += encode_uint32(1, w); // Number of disks.

	// End of central directory record, deferring everything to the zip64 one.
	w += encode_uint32(END_OF_CENTRAL_DIR_MAGIC, w);
	w += encode_uint16(0, w); // This disk.
	w += encode_uint16(0, w); // Disk with the central directory.
	w += encode_uint16(0xFFFF, w);
	w += encode_uint16(0xFFFF, w);
	w += encode_uint32(0xFFFFFFFF, w);
	w += encode_uint32(0xFFFFFFFF, w);
	w += encode_uint16(0, w); // Comment length.

	return buf;
}

void AppxPackager::init(Ref<FileAccess> p_fa) {
	package = p_fa;
	file_metadata.clear();
	central_dir_data.clear();
	central_dir_offset = 0;
	end_of_central_dir_offset = 0;
}

Error AppxPackager::add_file(const String &p_file_name, const uint8_t *p_buffer, size_t p_len, bool p_compress) {
	Source source;
	source.buffer = p_buffer;
	source.size = p_len;
	return _add(p_file_name, source, p_compress);
}

Error AppxPackager::add_file(const String &p_file_name, const String &p_source_path, bool p_compress) {
	Source source;
	Error err;
	source.file = FileAccess::open(p_source_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot open \"%s\".", p_source_path));
	source.size = source.file->get_length();
	return _add(p_file_name, source, p_compress);
}

Error AppxPackager::_add(const String &p_file_name, Source &p_source, bool p_compress) {
	ERR_FAIL_COND_V(package.is_null(), ERR_UNCONFIGURED);
	ERR_FAIL_COND_V_MSG(p_source.size >= 0xFFFFFFFF, ERR_INVALID_DATA, vformat("\"%s\" is 4 GiB or more, which the package headers cannot hold.", p_file_name));

	FileMeta meta;
	meta.name = p_file_name.utf8();
	meta.uncompressed_size = p_source.size;
	meta.compressed = p_compress;
	meta.zip_offset = package->get_position();

	// The header goes first with zero sizes and CRC, and is patched once the data is through.
	Vector<uint8_t> file_header = make_file_header(meta);
	meta.lfh_size = file_header.size();
	package->store_buffer(file_header.ptr(), file_header.size());

	Vector<uint8_t> in;
	in.resize(BLOCK_SIZE);
	Vector<uint8_t> out;
	z_stream strm{};
	if (p_compress) {
		out.resize(BLOCK_SIZE + 64);
		strm.zalloc = zipio_alloc;
		strm.zfree = zipio_free;
		deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
	}

	uLong crc = crc32(0L, Z_NULL, 0);
	uint64_t done = 0;
	while (done < p_source.size) {
		uint64_t block_size = MIN((uint64_t)BLOCK_SIZE, p_source.size - done);
		if (p_source.read(in.ptrw(), block_size) != block_size) {
			if (p_compress) {
				deflateEnd(&strm);
			}
			ERR_FAIL_V_MSG(ERR_FILE_CANT_READ, vformat("Short read while packing \"%s\".", p_file_name));
		}
		done += block_size;
		crc = crc32(crc, in.ptr(), block_size);

		BlockHash bh;
		bh.base64_hash = hash_block(in.ptr(), block_size);
		if (p_compress) {
			// A full flush per block, so the block map has one compressed size per block.
			strm.avail_in = block_size;
			strm.next_in = in.ptrw();
			strm.avail_out = out.size();
			strm.next_out = out.ptrw();
			uLong before = strm.total_out;
			int zerr = deflate(&strm, done == p_source.size ? Z_FINISH : Z_FULL_FLUSH);
			if (zerr < 0) {
				deflateEnd(&strm);
				ERR_FAIL_V_MSG(ERR_BUG, vformat("deflate failed (%d) while packing \"%s\".", zerr, p_file_name));
			}
			bh.compressed_size = strm.total_out - before;
			package->store_buffer(out.ptr(), bh.compressed_size);
		} else {
			bh.compressed_size = block_size;
			package->store_buffer(in.ptr(), block_size);
		}
		meta.hashes.push_back(bh);
	}

	if (p_compress) {
		if (p_source.size == 0) {
			// An empty deflate stream still has its end-of-stream block.
			strm.avail_in = 0;
			strm.avail_out = out.size();
			strm.next_out = out.ptrw();
			deflate(&strm, Z_FINISH);
			package->store_buffer(out.ptr(), strm.total_out);
		}
		meta.compressed_size = strm.total_out;
		deflateEnd(&strm);
	} else {
		meta.compressed_size = p_source.size;
	}
	meta.file_crc32 = crc;

	uint64_t end = package->get_position();
	package->seek(meta.zip_offset);
	file_header = make_file_header(meta);
	package->store_buffer(file_header.ptr(), file_header.size());
	package->seek(end);

	file_metadata.push_back(meta);
	return OK;
}

Error AppxPackager::finish() {
	ERR_FAIL_COND_V(package.is_null(), ERR_UNCONFIGURED);

	CharString block_map = make_block_map().utf8();
	Error err = add_file("AppxBlockMap.xml", (const uint8_t *)block_map.get_data(), block_map.length(), true);
	if (err == OK) {
		CharString content_types = make_content_types().utf8();
		err = add_file("[Content_Types].xml", (const uint8_t *)content_types.get_data(), content_types.length(), true);
	}
	if (err != OK) {
		package.unref();
		return err;
	}

	for (const FileMeta &file : file_metadata) {
		store_central_dir_header(file);
	}
	central_dir_offset = package->get_position();
	package->store_buffer(central_dir_data.ptr(), central_dir_data.size());

	end_of_central_dir_offset = package->get_position();
	Vector<uint8_t> end_record = make_end_of_central_record();
	package->store_buffer(end_record.ptr(), end_record.size());

	err = package->get_error();
	package.unref();
	return err;
}
