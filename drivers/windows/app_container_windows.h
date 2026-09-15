/**************************************************************************/
/*  app_container_windows.h                                               */
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

#include <windows.h>

#ifdef UWP_ENABLED
// An app container (WINAPI_FAMILY_APP) has no CreateFileW or MoveFileW; the file and directory
// drivers in this directory reach them through these, which forward to the app-partition
// functions with the same behavior.
inline HANDLE CreateFileW(LPCWSTR p_name, DWORD p_access, DWORD p_share, LPSECURITY_ATTRIBUTES p_security, DWORD p_disposition, DWORD p_flags_and_attributes, HANDLE p_template) {
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	params.dwFileAttributes = p_flags_and_attributes & 0x0000FFFF;
	params.dwFileFlags = p_flags_and_attributes & 0xFFF00000;
	params.dwSecurityQosFlags = p_flags_and_attributes & 0x000F0000;
	params.lpSecurityAttributes = p_security;
	params.hTemplateFile = p_template;
	return CreateFile2(p_name, p_access, p_share, p_disposition, &params);
}

inline BOOL MoveFileW(LPCWSTR p_from, LPCWSTR p_to) {
	return MoveFileExW(p_from, p_to, 0);
}
#endif
